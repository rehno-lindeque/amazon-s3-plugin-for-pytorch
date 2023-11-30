{ buildPythonPackage
, fetchFromGitHub
, fetchpatch
, lib
, cmake
, aws-sdk-cpp
, pybind11
, pytorch
, zlib
, curl

# temp
, pythonPackages
, pkg-config
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

    # Fixes downstream issue with dependent CMake configuration
    # See https://github.com/NixOS/nixpkgs/issues/70075#issuecomment-1019328864
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

  # postPatch = ''
  #   substituteInPlace setup.py \
  #     --replace \
  #       'extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))' \
  #       "extdir = '$out'"
  # '';

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
  enableParallelBuilding = false;

  # configurePhase = ''
  #   ls -r
  #   mkdir build && cd build
  #   cmake ../ -DCMAKE_BUILD_TYPE=Release 
  # '';

  # preBuildPhase = ''
  #   cat Makefile
  # '';

  # buildPhase = ''
  #   ls -r
  #   grep "/nix" $(find -name '*.cmake')
  #   make
  # '';

  # installPhase = ''
  #   make install
  # '';


  dontUseCmakeBuildDir = true;

  # postPatch = ''
  #   echo "..."
  #   # don't bother with setup.py's cmake build
  #   substituteInPlace setup.py \
  #     --replace "cmdclass=dict(build_ext=CMakeBuild)," ""
  # '';

  # preConfigure = ''
  #   substituteInPlace CMakeLists.txt \
  #     --replace "find_package(AWSSDK REQUIRED COMPONENTS s3 transfer)" ""
  # '';

  nativeBuildInputs = [
    cmake

    # tmp
    pkg-config
    zlib
    # curl
  ];

  buildInputs = [
    aws-sdk-cpp-s3-transfer
    pybind11

    #tmp
    # zlib
    curl
  ];

  checkInputs = [
  ];

  propagatedBuildInputs = [
    pytorch
    pythonPackages.pkgconfig
    pythonPackages.pybind11
  ];

  # preConfigure = ''
  #   export CMAKE_PREFIX_PATH=${pybind11}/share/cmake/pybind11:$CMAKE_PREFIX_PATH
  # '';


  # DEBUG
  preConfigure = ''
    printf "\n\n\n\n\n\n"
    echo "pre configure:"
    ls -Rl .
    printf "\n\n\n\n\n\n"
  '';
  postConfigure = ''
    printf "\n\n\n\n\n\n"
    echo "post configure:"
    ls -Rl .
    printf "\n\n\n\n\n\n"
  '';
  preBuild = ''
    printf "\n\n\n\n\n\n"
    echo "pre build:"
    ls -Rl .
    printf "\n\n\n\n\n\n"
  '';
  postBuild = ''
    printf "\n\n\n\n\n\n"
    echo "post build:"
    ls -Rl .
    printf "\n\n\n\n\n\n"
  '';
  preInstall = ''
    printf "\n\n\n\n\n\n"
    echo "pre install:"
    ls -Rl .
    printf "\n\n\n\n\n\n"
  '';
  postInstall = ''
    printf "\n\n\n\n\n\n"
    echo "post install:"
    ls -Rl .
    printf "\n\n\n\n\n\n"
  '';

  doCheck = true; # Doesn't appear to run any tests, but redundantly runs build_ext a second time for some reason

  # skip tests which do network calls
  checkPhase = ''
    # pytest tests -k 'not hist_in_tree and not branch_auto_interpretation'
    pytest tests/py-tests
    echo "..."
  '';

  # pythonImportsCheck = [ "awsio.python.lib.io.s3.s3dataset" ];

  meta = with lib; {
    description = "S3-plugin is a high performance PyTorch dataset library to efficiently access datasets stored in S3 buckets.";
    homepage = "https://github.com/aws/amazon-s3-plugin-for-pytorch";
    license = licenses.asl20;
    maintainers = with maintainers; [];
  };
}
