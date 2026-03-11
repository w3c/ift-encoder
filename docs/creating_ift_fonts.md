# Producing IFT Encoded Fonts

There are two main ways to produce IFT fonts using the command line utilities in this project:

1. Auto config: this auto generates an appropriate config for the font and performs conversion
   to IFT encoding with no additional configuration needed. This is the simplest way, but gives
   very little control over the particular segmentation of the font.

2. Multi-step: if manual control over the configuration and segmentation is desired then encoding
   can be done as a separate segmentation and then compilation operation where manually written
   configurations can be supplied to either step.

## font2ift with Auto Config

```bash
bazel run -c opt //util:font2ift -- \
  --input_font="$HOME/myfont/MyFont.ttf" \
  --output_path=$HOME/myfont/ift/ \
  --output_font="MyFont-IFT.woff2"
```

This will analyze the input font, decide how to segment it, and then produce the final IFT encoded font
and patches.

When utilizing auto config there are two optional flags which can be used to adjust the behaviour:
* `--auto_config_primary_script`: this tells the config generator which language/script the font is intended
  to be used with. It has two effects: first the codepoints of the primary script are eligible to be moved
  into the initial font. Second for scripts with large overlaps, such as CJK, primary script selects which
  of the overlapping scripts to use frequency data from. Values refer to frequency data files in
  [ift-encoder-data](https://github.com/w3c/ift-encoder-data/tree/main/data). Example values: "Script_bengali",
  "Language_fr"

* `--auto_config_quality`: This is analogous to a quality level in a compression library. It controls how much
  effort is spent to improve the efficiency of the final IFT font. Values range from 1 to 8, where higher
  values increase encoding times but typically result in a more efficient end IFT font (ie. less bytes
  transferred by clients using it).

Example command line with optional flags:

```bash
bazel run -c opt //util:font2ift -- \
  --input_font="$HOME/notosansjp/NotoSansJP-Regular.otf" \
  --output_path=$HOME/notosansjp/ \
  --output_font="NotoSansJP-Regular-IFT.woff2" \
  --auto_config_primary_script=Script_japanese \
  --auto_config_quality=3
```

To see a complete list of available flags run with --helpfull:

```bash
bazel run -c opt //util:font2ift -- --helpfull
```

*Note: the auto configuration mode is still under development, in particular the auto selection of quality level
is currently quite simplistic. It's expected to continue to evolve from it's current state.*

## Multi-Step (Advanced)

Under the hood IFT font encoding happens in three stages:

1. Generate or write a segmenter config for the font.
2. Generate a segmentation plan, which describes how the font is split into patches. Takes the segmenter config as an input.
3. Compile the final IFT encoded font following the segmentation plan.

For more advanced use cases these steps can be performed individually. This allows the segmenter config
and segmentation plans to be fine tuned beyond what auto configuration is capable of.

### Step 1: Generating a Segmenter Config

There are two main options for generating a segmenter config:

1. Write the config by hand, the segmenter is configured via an input configuration file using the
    [segmenter_config.proto](util/segmenter_config.proto) schema, see the comments there for more details.
    This option is useful when maximum control over segmentation parameters is needed, or custom frequency
    data is being supplied.

2. Auto generate the segmenter config using `util:gen_ift_segmenter_config`.

   ```
   CC=clang bazel run //util:gen_ift_segmenter_config -- \
     --quality=5 \
     --input_font=$HOME/MyFont.ttf > config.txtpb
   ```

   This analyzes the input font and tries to pick appropriate config values automatically. As discussed in
   the previous "Producing IFT Encoded Fonts" section there is a configurable quality level. If needed
   the auto generated config can be hand tweaked after generation.

### Step 2: Generating Segmentation Plan

Segmentation plans are in a [textproto format](https://protobuf.dev/reference/protobuf/textformat-spec/) using the
[segmentation_plan.proto](util/segmentation_plan.proto) schema. See the comments in the schema file for more information.

This repo currently provides a few experimental utilities that can generate segmentation plans for you. It is also
possible to write plans by hand, or develop new utilities to generate plans.

In this repo two options are currently provided:

1. [Recommended] `util/gen_ift_segmentation_plan`: this utility uses a subsetting closure based approach
    to generate a glyph keyed segmentation plan (extension segments that augment glyph data). It can optionally
    generate the table keyed portion of the config as well. Example execution:

    ```sh
    bazel run -c opt //util:gen_ift_segmentation_plan  -- \
      --input_font=$(pwd)/myfont.ttf \
      --config=path/to/config.textpb > segmentation_plan.txtpb
    ```

    The gen_ift_segmentation_plan tool is configured via an input configuration file using the
    [segmenter_config.proto](util/segmenter_config.proto) schema, see the comments there for more details.

    Note: this utility is under active development and still very experimental. See
    [the status section](docs/experimental/closure_glyph_segmentation.md#status) for more details.

2.  `util/gen_ift_table_keyed_plan`: this utility generates the table keyed (extension segments that augment non
    glyph data in the font) portion of a plan. Useful when not using the optional table keyed plan generation in
    `util/gen_ift_segmentation_plan`. Example execution:

    ```sh
    bazel run -c opt util:gen_ift_table_keyed_plan -- \
      --font=$(pwd)/myfont.ttf \
      latin.txt cyrillic.txt greek.txt > table_keyed_segmentation_plan.txtpb
    ```

If separate glyph keyed and table keyed configs were generated using #1 and #2 they can then be combined into one
complete plan by concatenating them:

```sh
cat glyph_keyed.txtpb table_keyed.txtpb > segmentation_plan.txtpb
```

For concrete examples of how to generate IFT fonts, see the [IFT Demo](https://github.com/garretrieger/ift-demo).
In particular the [Makefile](https://github.com/garretrieger/ift-demo/blob/main/Makefile) and the
[segmenter configs](https://github.com/garretrieger/ift-demo/tree/main/config) may be helpful.

### Step 3: Generating an IFT Encoding

Once a segmentation plan has been created it can be combined with the target font to produce an incremental font and collection of associated patches using the font2ift utility which is a wrapper around the compiler. Example execution:

```sh
bazel -c opt run util:font2ift  -- \
  --input_font=$(pwd)/myfont.ttf \
  --plan=$(pwd)/segmentation_plan.txtpb \
  --output_path=$(pwd)/out/ --output_font="myfont.ift.ttf"
```
