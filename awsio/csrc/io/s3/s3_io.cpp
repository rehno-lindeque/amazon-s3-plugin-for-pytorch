//
// Created by Nagmote, Roshani on 5/12/20.
//
#include "s3_io.h"

#include <aws/core/Aws.h>
#include <aws/core/utils/threading/Executor.h>
#include <aws/transfer/TransferManager.h>
#include <aws/core/config/AWSProfileConfigLoader.h>
#include <aws/core/utils/FileSystemUtils.h>
#include <aws/core/utils/StringUtils.h>
#include <aws/core/utils/logging/AWSLogging.h>
#include <aws/core/utils/logging/LogSystemInterface.h>
#include <aws/s3/S3Client.h>
#include <aws/s3/S3Errors.h>
#include <aws/core/utils/memory/AWSMemory.h>
#include <aws/core/utils/memory/stl/AWSStreamFwd.h>
#include <aws/core/utils/stream/PreallocatedStreamBuf.h>
#include <aws/core/utils/StringUtils.h>
#include <aws/s3/model/CompletedPart.h>
#include <aws/s3/model/GetObjectRequest.h>
#include <aws/s3/model/HeadBucketRequest.h>
#include <aws/s3/model/HeadObjectRequest.h>

#include <fstream>
#include <string>
#include "absl/strings/str_cat.h"

namespace awsio {
    namespace {
        static const uint64_t s3MultiPartDownloadChunkSize = 2 * 1024 * 1024;  // 50 MB
        static const int downloadRetries = 3;
        static const int64_t s3TimeoutMsec = 300000;
        static const int executorPoolSize = 25;

        Aws::Client::ClientConfiguration &setUpS3Config() {
            static Aws::Client::ClientConfiguration cfg;
            Aws::String config_file;
            // If AWS_CONFIG_FILE is set then use it, otherwise use ~/.aws/config.
            const char *config_file_env = getenv("AWS_CONFIG_FILE");
            if (config_file_env) {
                config_file = config_file_env;
            } else {
                const char *home_env = getenv("HOME");
                if (home_env) {
                    config_file = home_env;
                    config_file += "/.aws/config";
                }
            }
            Aws::Config::AWSConfigFileProfileConfigLoader loader(config_file);
            loader.Load();

            const char *use_https = getenv("S3_USE_HTTPS");
            if (use_https) {
                if (use_https[0] == '0') {
                    cfg.scheme = Aws::Http::Scheme::HTTP;
                } else {
                    cfg.scheme = Aws::Http::Scheme::HTTPS;
                }
            }
            const char *verify_ssl = getenv("S3_VERIFY_SSL");
            if (verify_ssl) {
                if (verify_ssl[0] == '0') {
                    cfg.verifySSL = false;
                } else {
                    cfg.verifySSL = true;
                }
            }
            return cfg;
        }

        void ShutdownClient(std::shared_ptr <Aws::S3::S3Client> *s3_client) {
            if (s3_client != nullptr) {
                delete s3_client;
                Aws::SDKOptions options;
                Aws::ShutdownAPI(options);
            }
        }

        void ShutdownTransferManager(std::shared_ptr <Aws::Transfer::TransferManager> *transfer_manager) {
            if (transfer_manager != nullptr) {
                delete transfer_manager;
            }
        }

        void parseS3Path(const std::string &fname, std::string *bucket, std::string *object) {
            std::string delimiter1 = "://";
            std::string delimiter2 = "/";

            std::string str = fname.substr(fname.find_first_of(delimiter1) + delimiter1.length(), std::string::npos);
            size_t last = 0;
            size_t next = 0;
            while ((next = str.find(delimiter2, last)) != std::string::npos) {
                *bucket = str.substr(last, next - last);
                last = next + 1;
            }
            *object = str.substr(last);
        }

        class S3FS {
        public:
            S3FS(const std::string &bucket, const std::string &object, const bool multi_part_download,
                 std::shared_ptr <Aws::Transfer::TransferManager> transfer_manager,
                 std::shared_ptr <Aws::S3::S3Client> s3_client)
                    : bucket_name_(bucket), object_name_(object), multi_part_download_(multi_part_download),
                      transfer_manager_(transfer_manager), s3_client_(s3_client) {}

            void read(const std::string &fname, uint64_t offset, size_t n, char *buffer) {
                //  multi_part_download = true;
                if (multi_part_download_) {
                    return readS3TransferManager(offset, n, buffer);
                } else {
                    return readS3Client(offset, n, buffer);
                }
            }

            void readS3Client(uint64_t offset, size_t n, char *buffer) {

                std::cout << "ReadFilefromS3 s3://" << this->bucket_name_ << "/" << this->object_name_ << " from "
                          << offset << " for n:" << n;

                Aws::S3::Model::GetObjectRequest getObjectRequest;

                getObjectRequest.WithBucket(this->bucket_name_.c_str()).WithKey(this->object_name_.c_str());

                std::string bytes = absl::StrCat("bytes=", offset, "-", offset + n - 1);

                getObjectRequest.SetRange(bytes.c_str());

                // When you don’t want to load the entire file into memory,
                // you can use IOStreamFactory in AmazonWebServiceRequest to pass a lambda to create a string stream.
                getObjectRequest.SetResponseStreamFactory([]() {
                    return Aws::New<Aws::StringStream>("S3IOAllocationTag");
                });
                // get the object
                auto getObjectOutcome = this->s3_client_->GetObject(getObjectRequest);

                if (!getObjectOutcome.IsSuccess()) {
                    auto error = getObjectOutcome.GetError();
                    std::cout << "ERROR: " << error.GetExceptionName() << ": "
                              << error.GetMessage() << std::endl;
                } else {
                    n = getObjectOutcome.GetResult().GetContentLength();
                    // read data as a block:
                    getObjectOutcome.GetResult().GetBody().read(buffer, n);
                    // buffer contains entire file
                    Aws::OFStream storeFile(object_name_.c_str(), Aws::OFStream::out | Aws::OFStream::trunc);
                    storeFile.write((const char *) (buffer), n);
                    storeFile.close();
                    std::cout << "File dumped to local file!" << std::endl;

                }
            }

            void readS3TransferManager(uint64_t offset, size_t n, char *buffer) {
                std::cout << "ReadFilefromS3 s3:// using Transfer Manager API: ";

                auto create_stream_fn = [&]() {  // create stream lambda fn
                    return Aws::New<S3UnderlyingStream>(
                            "S3ReadStream",
                            Aws::New<Aws::Utils::Stream::PreallocatedStreamBuf>("S3ReadStream",
                                                                                reinterpret_cast<unsigned char *>(buffer),
                                                                                n));
                };

                std::cout << "Created stream to read with transferManager";

                // This buffer is what we used to initialize streambuf and is in memory
                std::shared_ptr <Aws::Transfer::TransferHandle> downloadHandle =
                        this->transfer_manager_.get()->DownloadFile(this->bucket_name_.c_str(),
                                                                    this->object_name_.c_str(), offset, n,
                                                                    create_stream_fn);
                downloadHandle->WaitUntilFinished();
                std::cout << "File download to memory finished!" << std::endl;

                if (downloadHandle->GetStatus() != Aws::Transfer::TransferStatus::COMPLETED) {
                    auto error = downloadHandle->GetLastError();
                    std::cout << "ERROR: " << error.GetExceptionName() << ": "
                              << error.GetMessage() << std::endl;
                } else {
                    n = downloadHandle->GetBytesTotalSize();

                    // write buffered data to local file copy
                    Aws::OFStream storeFile(object_name_.c_str(), Aws::OFStream::out | Aws::OFStream::trunc);
                    storeFile.write((const char *) (buffer), downloadHandle->GetBytesTransferred());
                    storeFile.close();
                    std::cout << "File dumped to local file!" << std::endl;

                }
            }

        private:
            std::string bucket_name_;
            std::string object_name_;
            bool multi_part_download_;
            std::shared_ptr <Aws::S3::S3Client> s3_client_;
            std::shared_ptr <Aws::Transfer::TransferManager> transfer_manager_;
        };
    }

    std::shared_ptr <Aws::S3::S3Client> initializeS3Client() {
        Aws::SDKOptions options;
        options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Trace;

        Aws::InitAPI(options);
        // Set up the request
        auto s3_client = std::shared_ptr<Aws::S3::S3Client>(new Aws::S3::S3Client(
                setUpS3Config(), Aws::Client::AWSAuthV4Signer::PayloadSigningPolicy::Never, false));
        return s3_client;
    }

    std::shared_ptr <Aws::Utils::Threading::PooledThreadExecutor> initializeExecutor() {
        auto executor = Aws::MakeShared<Aws::Utils::Threading::PooledThreadExecutor>(
                "executor", executorPoolSize);
        return executor;
    }

    std::shared_ptr <Aws::Transfer::TransferManager> initializeTransferManager() {
        Aws::Transfer::TransferManagerConfiguration transfer_config(initializeExecutor().get());
        transfer_config.s3Client = initializeS3Client();

        // This buffer is what we used to initialize streambuf and is in memory
        transfer_config.bufferSize = s3MultiPartDownloadChunkSize;
        transfer_config.transferBufferMaxHeapSize = (executorPoolSize + 1) * s3MultiPartDownloadChunkSize;
        auto transfer_manager = Aws::Transfer::TransferManager::Create(transfer_config);
        return transfer_manager;
    }


//class S3Init {
//public:
    S3Init::S3Init() : s3_client_(nullptr, ShutdownClient), transfer_manager_(nullptr, ShutdownTransferManager) {}

    S3Init::~S3Init() {}

    void S3Init::s3_read(std::string file_url, bool use_tm) {
        std::string bucket, object;
        parseS3Path(file_url, &bucket, &object);
        S3FS s3handler(bucket, object, use_tm, initializeTransferManager(), initializeS3Client());
    }
//};

}