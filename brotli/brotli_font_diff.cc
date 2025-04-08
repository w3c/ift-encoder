#include "brotli/brotli_font_diff.h"

#include "absl/types/span.h"
#include "brotli/brotli_stream.h"
#include "brotli/glyf_differ.h"
#include "brotli/hmtx_differ.h"
#include "brotli/loca_differ.h"
#include "brotli/table_range.h"
#include "common/int_set.h"

namespace brotli {

using absl::Span;
using absl::Status;
using common::FontData;
using common::IntSet;

static bool HasTable(hb_face_t* face, hb_tag_t tag) {
  hb_blob_t* table = hb_face_reference_table(face, tag);
  bool non_empty = (table != hb_blob_get_empty());
  hb_blob_destroy(table);
  return non_empty;
}

static bool HasTable(hb_face_t* base, hb_face_t* derived, hb_tag_t tag) {
  return HasTable(base, tag) && HasTable(derived, tag);
}

/*
 * Writes out a brotli encoded copy of the 'derived' subsets glyf table using
 * the 'base' subset as a shared dictionary.
 *
 * Performs the comparison using the glyph ids in the plans for each subset and
 * does not actually compare any glyph bytes. Common ranges are glyphs are
 * encoded using backwards references to the base dictionary. Novel glyph data
 * found in 'derived' is encoded as compressed data without the use of the
 * shared dictionary.
 */
class DiffDriver {
  struct RangeAndDiffer {
    RangeAndDiffer(hb_face_t* base_face, hb_face_t* derived_face, hb_tag_t tag,
                   const BrotliStream& base_stream, TableDiffer* differ_)
        : range(base_face, derived_face, tag, base_stream), differ(differ_) {}

    TableRange range;
    std::unique_ptr<TableDiffer> differ;
  };

 public:
  DiffDriver(hb_subset_plan_t* base_plan, hb_face_t* base_face,
             hb_subset_plan_t* derived_plan, hb_face_t* derived_face,
             const IntSet& custom_diff_tables, BrotliStream& stream)
      : out(stream),
        base_new_to_old(hb_subset_plan_new_to_old_glyph_mapping(base_plan)),
        derived_old_to_new(
            hb_subset_plan_old_to_new_glyph_mapping(derived_plan)) {
    hb_blob_t* head =
        hb_face_reference_table(derived_face, HB_TAG('h', 'e', 'a', 'd'));
    const char* head_data = hb_blob_get_data(head, nullptr);
    unsigned is_derived_short_loca = !head_data[51];
    hb_blob_destroy(head);

    head = hb_face_reference_table(base_face, HB_TAG('h', 'e', 'a', 'd'));
    head_data = hb_blob_get_data(head, nullptr);
    unsigned is_base_short_loca = !head_data[51];
    hb_blob_destroy(head);

    base_glyph_count = hb_face_get_glyph_count(base_face);
    derived_glyph_count = hb_face_get_glyph_count(derived_face);

    retain_gids = base_glyph_count > hb_map_get_population(base_new_to_old);

    constexpr hb_tag_t HMTX = HB_TAG('h', 'm', 't', 'x');
    constexpr hb_tag_t VMTX = HB_TAG('v', 'm', 't', 'x');
    constexpr hb_tag_t HHEA = HB_TAG('h', 'h', 'e', 'a');
    constexpr hb_tag_t VHEA = HB_TAG('v', 'h', 'e', 'a');
    constexpr hb_tag_t LOCA = HB_TAG('l', 'o', 'c', 'a');
    constexpr hb_tag_t GLYF = HB_TAG('g', 'l', 'y', 'f');

    for (hb_tag_t tag : custom_diff_tables) {
      switch (tag) {
        case HMTX:
          if (HasTable(base_face, derived_face, HMTX) &&
              HasTable(base_face, derived_face, HHEA)) {
            differs.push_back(RangeAndDiffer(
                base_face, derived_face, HMTX, stream,
                new HmtxDiffer(TableRange::to_span(base_face, HHEA),
                               TableRange::to_span(derived_face, HHEA))));
          }
          break;

        case VMTX:
          if (HasTable(base_face, derived_face, VMTX) &&
              HasTable(base_face, derived_face, VHEA)) {
            differs.push_back(RangeAndDiffer(
                base_face, derived_face, VMTX, stream,
                new HmtxDiffer(TableRange::to_span(base_face, VHEA),
                               TableRange::to_span(derived_face, VHEA))));
          }
          break;

        case LOCA:
          if (HasTable(base_face, derived_face, GLYF) &&
              HasTable(base_face, derived_face, LOCA)) {
            differs.push_back(RangeAndDiffer(
                base_face, derived_face, LOCA, stream,
                new LocaDiffer(is_base_short_loca, is_derived_short_loca)));
          }
          break;

        case GLYF:
          if (HasTable(base_face, derived_face, GLYF) &&
              HasTable(base_face, derived_face, LOCA)) {
            differs.push_back(RangeAndDiffer(
                base_face, derived_face, GLYF, stream,
                new GlyfDiffer(TableRange::to_span(derived_face, LOCA),
                               is_base_short_loca, is_derived_short_loca)));
          }
          break;
      }
    }
  }

 public:
  std::vector<RangeAndDiffer> differs;

 private:
  BrotliStream& out;

  unsigned base_gid = 0;
  unsigned derived_gid = 0;

  const hb_map_t* base_new_to_old;
  const hb_map_t* derived_old_to_new;

  unsigned base_glyph_count;
  unsigned derived_glyph_count;

  bool retain_gids;

 public:
  Status MakeDiff() {
    // Notation:
    // base_gid:      glyph id in the base subset glyph space.
    // *_derived_gid: glyph id in the derived subset glyph space.
    // *_old_gid:     glyph id in the original font glyph space.

    while (derived_gid < derived_glyph_count) {
      bool is_base_empty = false;
      unsigned base_derived_gid = BaseToDerivedGid(base_gid, &is_base_empty);
      if (is_base_empty && derived_gid == base_derived_gid &&
          hb_map_has(derived_old_to_new, derived_gid)) {
        // base and derived are the same glyph but base is empty while
        // derived is not. This means the gids will match but these glyphs
        // are not the same, so set based_derived_gid to invalid to trigger
        // the diff to treat this as new data.
        base_derived_gid = HB_MAP_VALUE_INVALID;
      }

      for (auto& range_and_differ : differs) {
        TableDiffer* differ = range_and_differ.differ.get();
        TableRange& range = range_and_differ.range;

        bool was_new_data = differ->IsNewData();
        unsigned base_length = 0;
        unsigned derived_length = 0;
        differ->Process(derived_gid, base_gid, base_derived_gid, is_base_empty,
                        &base_length, &derived_length);

        if (derived_gid > 0 && was_new_data != differ->IsNewData()) {
          if (was_new_data) {
            Status s = range.CommitNew();
            if (!s.ok()) {
              return s;
            }
          } else {
            range.CommitExisting();
          }
        }

        range.Extend(base_length, derived_length);
      }

      if (base_derived_gid == derived_gid ||
          (base_gid == derived_gid && is_base_empty)) {
        base_gid++;
      }
      derived_gid++;
    }

    // Finalize and commit any outstanding changes.
    for (auto& range_and_differ : differs) {
      TableDiffer* differ = range_and_differ.differ.get();
      TableRange& range = range_and_differ.range;
      unsigned base_length = 0;
      unsigned derived_length = 0;
      differ->Finalize(&base_length, &derived_length);
      range.Extend(base_length, derived_length);
      if (differ->IsNewData()) {
        Status s = range.CommitNew();
        if (!s.ok()) {
          return s;
        }
      } else {
        range.CommitExisting();
      }
      range.stream().four_byte_align_uncompressed();
      out.append(range.stream());
    }

    return absl::OkStatus();
  }

 private:
  unsigned BaseToDerivedGid(unsigned gid, bool* is_base_empty) {
    if (retain_gids) {
      if (gid < base_glyph_count) {
        // If retain gids is set gids are equivalent in all three spaces.
        *is_base_empty = !hb_map_has(base_new_to_old, gid);
        return gid;
      }
      return HB_MAP_VALUE_INVALID;
    }

    *is_base_empty = false;
    unsigned base_old_gid = hb_map_get(base_new_to_old, gid);
    return hb_map_get(derived_old_to_new, base_old_gid);
  }
};

void BrotliFontDiff::SortForDiff(const IntSet& immutable_tables,
                                 const IntSet& custom_diff_tables,
                                 const hb_face_t* original_face,
                                 hb_face_t* face_builder) {
  // Place generic diff tables,
  // then immutable tables,
  // then custom diff tables.
  std::vector<hb_tag_t> table_order;
  hb_tag_t table_tags[32];
  unsigned offset = 0, num_tables = 32;
  while (((void)hb_face_get_table_tags(original_face, offset, &num_tables,
                                       table_tags),
          num_tables)) {
    for (unsigned i = 0; i < num_tables; ++i) {
      hb_tag_t tag = table_tags[i];
      if (!immutable_tables.contains(tag) &&
          !custom_diff_tables.contains(tag)) {
        table_order.push_back(tag);
      }
    }
    offset += num_tables;
  }

  for (hb_codepoint_t tag : immutable_tables) {
    table_order.push_back(tag);
  }

  for (hb_codepoint_t tag : custom_diff_tables) {
    table_order.push_back(tag);
  }

  table_order.push_back(0);

  hb_face_builder_sort_tables(face_builder, table_order.data());
}

Status BrotliFontDiff::Diff(hb_subset_plan_t* base_plan, hb_blob_t* base,
                            hb_subset_plan_t* derived_plan, hb_blob_t* derived,
                            FontData* patch) const {
  Span<const uint8_t> base_span = TableRange::to_span(base);
  Span<const uint8_t> derived_span = TableRange::to_span(derived);

  // get a 'real' (non facebuilder) face for the faces.
  hb_face_t* derived_face = hb_face_create(derived, 0);
  hb_face_t* base_face = hb_face_create(base, 0);

  BrotliStream out(
      BrotliStream::WindowBitsFor(base_span.size(), derived_span.size()),
      base_span.size());

  unsigned derived_start_offset = 0;
  unsigned derived_end_offset = 0;
  unsigned base_start_offset = 0;
  unsigned base_end_offset = 0;

  DiffDriver diff_driver(base_plan, base_face, derived_plan, derived_face,
                         custom_diff_tables_, out);

  const IntSet* tag_sets[] = {&immutable_tables_, &custom_diff_tables_};
  unsigned base_region_sizes[] = {0, 0};
  unsigned i = 0;
  for (const IntSet* set : tag_sets) {
    for (hb_tag_t tag : *set) {
      if (!HasTable(derived_face, tag)) {
        continue;
      }

      if (HasTable(base_face, tag) != HasTable(derived_face, tag)) {
        return absl::InternalError(
            "base and derived must both have the same tables.");
      }

      Span<const uint8_t> base_span =
          TableRange::padded_table_span(TableRange::to_span(base_face, tag));
      Span<const uint8_t> derived_span =
          TableRange::padded_table_span(TableRange::to_span(derived_face, tag));

      base_region_sizes[i] += base_span.size();

      unsigned base_offset = TableRange::table_offset(base_face, tag);
      unsigned derived_offset = TableRange::table_offset(derived_face, tag);

      if (!derived_start_offset) {
        derived_start_offset = derived_offset;
      }

      if (!base_start_offset) {
        base_start_offset = base_offset;
      }

      if (derived_end_offset && derived_end_offset != derived_offset) {
        return absl::InternalError(
            "custom diff tables in derived are not sequential.");
      }

      if (base_end_offset && base_end_offset != base_offset) {
        return absl::InternalError(
            "custom diff tables in base are not sequential.");
      }

      derived_end_offset = derived_offset + derived_span.size();
      base_end_offset = base_offset + base_span.size();
    }
    i++;
  }

  Status s = out.insert_compressed_with_partial_dict(
      derived_span.subspan(0, derived_start_offset),
      base_span.subspan(0, base_start_offset));
  if (!s.ok()) {
    return s;
  }

  if (!out.insert_from_dictionary(base_start_offset, base_region_sizes[0])) {
    return absl::InternalError("dict insert of immutable tables failed.");
  }

  s = diff_driver.MakeDiff();
  if (!s.ok()) {
    return s;
  }

  if (derived_span.size() > derived_end_offset) {
    s = out.insert_compressed(derived_span.subspan(
        derived_end_offset, derived_end_offset - derived_span.size()));
    if (!s.ok()) {
      return s;
    }
  }

  out.end_stream();

  patch->copy((const char*)out.compressed_data().data(),
              out.compressed_data().size());

  hb_face_destroy(base_face);
  hb_face_destroy(derived_face);

  return absl::OkStatus();
}

}  // namespace brotli
