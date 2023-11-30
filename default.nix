{ buildPythonPackage
, fetchFromGitHub
, fetchpatch
, lib
, cmake
, aws-sdk-cpp
, pybind11
, pytorch
, zlib
}:

let
  aws-sdk-cpp-s3-transfer = (aws-sdk-cpp.override {
    apis = ["s3" "transfer"];
    customMemoryManagement = false;
  # });
  }).overrideAttrs (oldAttrs: {
    patches = [
      # (fetchpatch {
      #   url = "https://github.com/aws/aws-sdk-cpp/commit/42991ab549087c81cb630e5d3d2413e8a9cf8a97.patch";
      #   sha256 = "0myq5cm3lvl5r56hg0sc0zyn1clbkd9ys0wr95ghw6bhwpvfv8gr";
      # })
      ./cmake-dirs.patch
    ];

    postPatch = oldAttrs.postPatch + ''
      substituteInPlace cmake/AWSSDKConfig.cmake \
        --replace "\''${AWSSDK_DEFAULT_ROOT_DIR}/\''${AWSSDK_INSTALL_INCLUDEDIR}" "\''${AWSSDK_INSTALL_INCLUDEDIR}"
    '';
});
in
buildPythonPackage rec {
  pname = "amazon-s3-plugin-for-pytorch";

  version = "38284c8";

  # src = fetchFromGitHub {
  #   owner = "aws";
  #   repo = pname;
  #   rev = "38284c8a5e92be3bbf47b08e8c90d94be0cb79e7";
  #   sha256 = "0lww4y3mq854za95hi4y441as4r3h3q0nyrpgj4j6kjk58gijbx9";
  # };
  src = ./.;

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
    # "-DCMAKE_PREFIX_PATH=${aws-sdk-cpp-s3-transfer.dev}/lib/cmake"
    # "-DAWSSDK_ROOT_DIR=${aws-sdk-cpp-s3-transfer.dev}"
    # "-DAWSSDK_ROOT_DIR="
    # "-DAWSSDK_INSTALL_INCLUDEDIR=${aws-sdk-cpp-s3-transfer.dev}/include"
    # "-DAWSSDK_INSTALL_LIBDIR=${aws-sdk-cpp-s3-transfer.dev}/lib"
    # "-DAWSSDK_INSTALL_INCLUDEDIR=include"
    # "--config" "Release"
    # "-DCMAKE_CXX_FLAGS=-fPIC"
    # "-DCMAKE_BUILD_TYPE=Release"
  ];

  dontUseCmakeBuildDir = true;

  postPatch = ''
    # don't bother with setup.py's cmake build
    substituteInPlace setup.py \
      --replace "cmdclass=dict(build_ext=CMakeBuild)," ""
  '';

  # preConfigure = ''
  #   substituteInPlace CMakeLists.txt \
  #     --replace "find_package(AWSSDK REQUIRED COMPONENTS s3 transfer)" ""
  # '';

  nativeBuildInputs = [
    cmake
    # zlib
  ];

  buildInputs = [
    aws-sdk-cpp-s3-transfer
    pybind11
    # zlib
  ];

  checkInputs = [
  ];

  propagatedBuildInputs = [
    pytorch
  ];

  doCheck = false; # ?

  pythonImportsCheck = [ "awsio.python.lib.io.s3.s3dataset" ];

  meta = with lib; {
    description = "S3-plugin is a high performance PyTorch dataset library to efficiently access datasets stored in S3 buckets.";
    homepage = "https://github.com/aws/amazon-s3-plugin-for-pytorch";
    license = licenses.asl20;
    maintainers = with maintainers; [];
  };
}
