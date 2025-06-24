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

## Producing IFT Encoded Fonts

IFT encoded fonts are produced in two steps:
1. A segmentation plan is generated which specifies how the font file should be split up in the IFT encoding.
2. The IFT encoded font and patches are compiled by the Compiler sub module using the segmentation plan.

### Generating Segmentation Plan

Segmentation plans are in a [textproto format](https://protobuf.dev/reference/protobuf/textformat-spec/) using the
[segmentation_plan.proto](util/segmentation_plan.proto) schema. See the comments in the schema file for more information.

This repo currently provides a few experimental utilities that can generate segmentation plans for you. It is also
possible to write plans by hand, or develop new utilities to generate plans.

In this repo 3 options are currently provided:

1.  `util/generate_table_keyed_config`: this utility generates the table keyed (extension segments that augment non
    glyph data in the font) portion of a plan. Example execution:

    ```sh
    bazel run -c opt util:generate_table_keyed_config -- \
      --font=$(pwd)/myfont.ttf \
      latin.txt cyrillic.txt greek.txt > table_keyed.txtpb
    ```

2.  `util/closure_glyph_keyed_segmenter_util`: this utility uses a subsetting closure based approach to generate a glyph
    keyed segmentation plan (extension segments that augment glyph data). Example execution:

    ```sh
    bazel run -c opt util:closure_glyph_keyed_segmenter_util  -- \
      --input_font=$(pwd)/myfont.ttf \
      --number_of_segments=20 \
      --codepoints_file=$(pwd)/all_cps.txt \
      --output_segmentation_plan > glyph_keyed.txtpb
    ```

    Note: this utility is under active development and still very experimental. See
    [the status section](docs/experimental/closure_glyph_segmentation.md#status) for more details.

3.  `util/iftb2config`: this utility converts a segmentation obtained from the
    [binned incremental font transfer prototype](https://github.com/adobe/binned-ift-reference)
    into and equivalent segmentation plan. Example execution:

    ```sh
    iftb -VV info my_iftb_font.ttf 2>&1 | \
      bazel run util:iftb2config > segmentation_plan.txtpb
    ```

If seperate glyph keyed and table keyed configs were generated using #1 and #2 they can then be combined into one
complete plan by concatenating them:

```sh
cat glyph_keyed.txtpb table_keyed.txtpb > segmentation_plan.txtpb
```

Additional tools for generating encoder configs are planned to be added in the future.

### Generating an IFT Encoding

Once an segmentation plan has been created it can be combined with the target font to produce and incremental font and collection
of associated patches using the font2ift utility which is a wrapper around the compiler. Example execution:

```sh
bazel -c opt run util:font2ift  -- \
  --input_font=$(pwd)/myfont.ttf \
  --plan=$(pwd)/segmentation_plan.txtpb \
  --output_path=$(pwd)/out/ --output_font="myfont.ift.ttf"
```
