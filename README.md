# Incremental Font Transfer Encoder

This code repository contains an implementation of an [incremental font transfer](https://w3c.github.io/IFT/Overview.html) encoder.

Currently this implements a "low level" encoder where a specific segmentation plan  (how code points, features, and design space are
split between patches) must be provided to generate the encoding. Eventually a higher level interface will be implemented which is
capable of generating a segmentation plan.

The encoder functionality can either be accessed programmatically via ift/encoder.h or via a command line tool.

## Producing IFT Encoded Fonts

IFT encoded fonts are produced in two steps:
1. An encoding config is generated which specifies how the font file should be split up in the IFT encoding.
2. The IFT encoded font and patches are generated using the encoding config.

### Generating Encoding Configs

TODO...

### Generating the IFT Encoding

TODO...

The font2ift command line tool can be used to convert a non incremental font into an incremental font and collection of associated
patches. Example usage:

```sh
bazel run util:font2ift  -- --input_font=$(pwd)/myfont.ttf --config=$(pwd)/segmentation_plan.txtpb --output_path=$(pwd)/ --output_font="myfont.ift.ttf"
```

Where segmentation_plan.textproto is a textproto file using the util/encoder_config.h schema. See the comments in that file for more details.

## Build

This repository uses the bazel build system. You can build everything:

```sh
bazel build ...
```

and run all of the tests:

```sh
bazel test ...
```

## Code Style

The code follows the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html). Formatting is enforced by an automated check for new
commits to this repo. You can auto-correct formatting for all files using the format.sh script.
