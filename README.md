# Incremental Font Transfer Encoder

This code repository contains an implementation of an
[incremental font transfer](https://w3c.github.io/IFT/Overview.html) encoder.

## Status

This implementation is still in the early stages and as at the moment is work in progress.
We aim to keep it updated and consistent with the current IFT specification working draft
found [here](https://w3c.github.io/IFT/Overview.html).

The current implementation is capable of producing a spec-compliant encoding, but does not
yet fully support all aspects of the specification. Notably:

*  Format 1 patch maps are not generated.
*  Not all encoder config options are supported yet. These are marked as unimplemented in the schema.

Additionally, the produced encodings may not be fully optimized for minimal size yet.

See this repos issue tracker for a more complete list of missing functionality.

## Building and Testing

This repository uses the bazel build system. You can build everything:

```sh
bazel build ...
```

and run all of the tests:

```sh
bazel test ...
```

### Building without Dependency Graph Support

By default this depends on the experimental harfbuzz dependency graph API which isn't yet in mainline harfbuzz.
The dependency graph functionality can be disabled at compile time using the `harfbuzz_dep_graph` build flag.
For example:

```sh
bazel build --//:harfbuzz_dep_graph=False ...
bazel test --//:harfbuzz_dep_graph=False ...
```

Disabling the harfbuzz dependency graph API will cause segmenter runs using the `CLOSURE_AND_DEP_GRAPH` and `CLOSURE_AND_VALIDATE_DEP_GRAPH` condition analysis modes to fail.

## Producing IFT Encoded Fonts

This project provides command line utilities and a C++ API which can be used to produce IFT encoded
fonts. IFT encoding works by taking an existing TrueType or OpenType font and then produces an IFT
encoded version which splits the functionality of the input font into a set of patches.

There are two main phases to producing an IFT font:
* Segmentation: this process analyzes the input font and decides how to best split the font across
  patches to maximize performance.
* Compilation: takes the font and the segmentation and produces the IFT font and patch files.

For more information see the documents under [docs/experimental](docs/experimental). Of note:
* [compiler.md](docs/experimental/compiler.md)
* [closure_glyph_segmentation.md](docs/experimental/closure_glyph_segmentation.md)
* [closure_glyph_segmentation_merging.md](docs/experimental/closure_glyph_segmentation_merging.md)
* [closure_glyph_segmentation_complex_conditions.md](docs/experimental/closure_glyph_segmentation_complex_conditions.md)

### font2ift with Auto Config

The simplest way to create IFT fonts is via the `font2ift` utility utilizing the auto configuration mode.
This is done by running the utility and not providing a segmentation plan. Example invocation:

```bash
bazel run -c opt //util:font2ift -- \
  --input_font="$HOME/myfont/MyFont.ttf" \
  --output_path=$HOME/myfont/ift/ \
  --output_font="MyFont-IFT.woff2"
```

This will analyze the input font, decide how to segment it, and then produce the final IFT encoded font
and patches.

See [creating_ift_fonts.md](docs/creating_ift_fonts.md) for more details. That document also discusses
advanced techniques for generating IFT fonts which allow more control via manual configuration.

### Encoder API

The auto configuration, segmentation, and compilation encoder functionality can also all be accessed
via C++ APIs:

* [ift/config/auto_segmenter_config.h](ift/config/auto_segmenter_config.h): API for auto generating
  segmenter configs.

* [ift/config/segmenter_config_util.h](ift/config/segmenter_config_util.h): API for loading segmenter
  configs, and provides a convenient way to run the segmenter directly from a config.

* [ift/config/load_codepoints.h](ift/config/load_codepoints.h): API for loading code point frequency data
  (including the built in data).

* [ift/encoder/closure_glyph_segmenter.h](ift/encoder/closure_glyph_segmenter.h): API for generating segmentation
  plans for a font.

* [ift/encoder/compiler.h](ift/encoder/compiler.h): API for compiling a font and segmentation plan into an
  IFT font.

For example usage of these APIs see [font2ift](util/font2ift.cc). It has code demonstrating how to integrating auto
configuration, segmenting, and compilation to go from an input font to an IFT font.

Note: these APIs are not yet stabilized and may change as the project evolves.

## Code Style

The code follows the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html). Formatting is enforced by an automated check for new commits to this repo. You can auto-correct formatting for all files using the check-format.sh
script:

```sh
./check-format.sh --fix
```

## Generating compile_commands.json for IDE

This repo is configured to use [hedron](https://github.com/hedronvision/bazel-compile-commands-extractor) to produce a
compile_commands.json file which can be provided to some IDEs to configure on the fly compilation and auto complete of
source files:

```
bazel run @hedron_compile_commands//:refresh_all
```

Will generate a compile_commands.json file.
