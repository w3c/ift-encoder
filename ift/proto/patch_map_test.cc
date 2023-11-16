#include "ift/proto/patch_map.h"

#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>

#include <cstdio>
#include <cstring>

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/types/span.h"
#include "common/font_data.h"
#include "common/font_helper.h"
#include "common/hb_set_unique_ptr.h"
#include "common/sparse_bit_set.h"
#include "gtest/gtest.h"
#include "ift/proto/IFT.pb.h"

using absl::flat_hash_map;
using absl::flat_hash_set;
using absl::Span;
using absl::Status;
using absl::StrCat;
using common::FontData;
using common::FontHelper;
using common::hb_set_unique_ptr;
using common::make_hb_set;
using common::SparseBitSet;
using google::protobuf::TextFormat;
using ::google::protobuf::util::MessageDifferencer;

namespace ift::proto {

class PatchMapTest : public ::testing::Test {
 protected:
  PatchMapTest() {
    sample.set_url_template("fonts/go/here");
    sample.set_default_patch_encoding(SHARED_BROTLI_ENCODING);

    auto m = sample.add_subset_mapping();
    hb_set_unique_ptr set = make_hb_set(2, 7, 9);
    m->set_bias(23);
    m->set_codepoint_set(SparseBitSet::Encode(*set.get()));
    m->set_id_delta(0);

    m = sample.add_subset_mapping();
    set = make_hb_set(3, 10, 11, 12);
    m->set_bias(45);
    m->set_codepoint_set(SparseBitSet::Encode(*set.get()));
    m->set_id_delta(0);
    m->set_patch_encoding(IFTB_ENCODING);

    overlap_sample = sample;

    m = overlap_sample.add_subset_mapping();
    set = make_hb_set(1, 55);
    m->set_bias(0);
    m->set_codepoint_set(SparseBitSet::Encode(*set.get()));
    m->set_id_delta(0);

    complex_ids.set_url_template("fonts/go/here");
    complex_ids.set_default_patch_encoding(SHARED_BROTLI_ENCODING);

    m = complex_ids.add_subset_mapping();
    set = make_hb_set(1, 0);
    m->set_bias(0);
    m->set_codepoint_set(SparseBitSet::Encode(*set.get()));
    m->set_id_delta(-1);

    m = complex_ids.add_subset_mapping();
    set = make_hb_set(1, 5);
    m->set_bias(0);
    m->set_codepoint_set(SparseBitSet::Encode(*set.get()));
    m->set_id_delta(4);

    m = complex_ids.add_subset_mapping();
    set = make_hb_set(1, 2);
    m->set_bias(0);
    m->set_codepoint_set(SparseBitSet::Encode(*set.get()));
    m->set_id_delta(-4);

    m = complex_ids.add_subset_mapping();
    set = make_hb_set(1, 4);
    m->set_bias(0);
    m->set_codepoint_set(SparseBitSet::Encode(*set.get()));
    m->set_id_delta(1);
  }

  IFT empty;
  IFT sample;
  IFT overlap_sample;
  IFT complex_ids;
};

std::string Diff(const IFT& a, const IFT& b) {
  std::string a_str, b_str;
  TextFormat::PrintToString(a, &a_str);
  TextFormat::PrintToString(b, &b_str);
  return StrCat("Expected:\n", a_str, "\n", "Actual:\n", b_str);
}

TEST_F(PatchMapTest, AddFromProto) {
  PatchMap map;
  auto s = map.AddFromProto(sample);
  s.Update(map.AddFromProto(complex_ids, true));
  ASSERT_TRUE(s.ok()) << s;

  PatchMap expected = {
      {{30, 32}, 1, SHARED_BROTLI_ENCODING},
      {{55, 56, 57}, 2, IFTB_ENCODING},
      {{0}, 0, SHARED_BROTLI_ENCODING, true},
      {{5}, 5, SHARED_BROTLI_ENCODING, true},
      {{2}, 2, SHARED_BROTLI_ENCODING, true},
      {{4}, 4, SHARED_BROTLI_ENCODING, true},
  };

  ASSERT_EQ(map, expected);
}

TEST_F(PatchMapTest, Empty) {
  auto map = PatchMap::FromProto(empty);
  ASSERT_TRUE(map.ok()) << map.status();
  PatchMap expected = {};
  ASSERT_EQ(*map, expected);
}

TEST_F(PatchMapTest, GetEntries) {
  auto map = PatchMap::FromProto(sample);
  ASSERT_TRUE(map.ok()) << map.status();

  PatchMap::Entry entries[] = {
      {{30, 32}, 1, SHARED_BROTLI_ENCODING},
      {{55, 56, 57}, 2, IFTB_ENCODING},
  };
  Span<const PatchMap::Entry> expected(entries);

  ASSERT_EQ(map->GetEntries(), expected);
}

TEST_F(PatchMapTest, Mapping) {
  auto map = PatchMap::FromProto(sample);
  ASSERT_TRUE(map.ok()) << map.status();

  PatchMap expected = {
      {{30, 32}, 1, SHARED_BROTLI_ENCODING},
      {{55, 56, 57}, 2, IFTB_ENCODING},
  };

  ASSERT_EQ(*map, expected);
}

TEST_F(PatchMapTest, Mapping_ComplexIds) {
  auto map = PatchMap::FromProto(complex_ids);
  ASSERT_TRUE(map.ok()) << map.status();

  PatchMap expected = {
      {{0}, 0, SHARED_BROTLI_ENCODING},
      {{5}, 5, SHARED_BROTLI_ENCODING},
      {{2}, 2, SHARED_BROTLI_ENCODING},
      {{4}, 4, SHARED_BROTLI_ENCODING},
  };

  ASSERT_EQ(*map, expected);
}

TEST_F(PatchMapTest, Mapping_Overlaping) {
  auto map = PatchMap::FromProto(overlap_sample);
  ASSERT_TRUE(map.ok()) << map.status();

  PatchMap expected = {
      {{30, 32}, 1, SHARED_BROTLI_ENCODING},
      {{55, 56, 57}, 2, IFTB_ENCODING},
      {{55}, 3, SHARED_BROTLI_ENCODING},
  };

  ASSERT_EQ(*map, expected);
}

TEST_F(PatchMapTest, AddPatch) {
  auto map = PatchMap::FromProto(sample);
  ASSERT_TRUE(map.ok()) << map.status();

  map->AddEntry({77, 79, 80}, 5, SHARED_BROTLI_ENCODING);

  PatchMap expected = {
      {{30, 32}, 1, SHARED_BROTLI_ENCODING},
      {{55, 56, 57}, 2, IFTB_ENCODING},
      {{77, 79, 80}, 5, SHARED_BROTLI_ENCODING},
  };

  ASSERT_EQ(*map, expected);

  map->AddEntry({1, 2, 3}, 3, IFTB_ENCODING);

  expected = {
      {{30, 32}, 1, SHARED_BROTLI_ENCODING},
      {{55, 56, 57}, 2, IFTB_ENCODING},
      {{77, 79, 80}, 5, SHARED_BROTLI_ENCODING},
      {{1, 2, 3}, 3, IFTB_ENCODING},
  };

  ASSERT_EQ(*map, expected);
}

TEST_F(PatchMapTest, RemoveEntries) {
  auto map = PatchMap::FromProto(sample);
  ASSERT_TRUE(map.ok()) << map.status();

  ASSERT_EQ(map->RemoveEntries(1), PatchMap::MODIFIED_MAIN);

  PatchMap expected = {
      {{55, 56, 57}, 2, IFTB_ENCODING},
  };

  ASSERT_EQ(*map, expected);
}

TEST_F(PatchMapTest, RemoveEntries_Multiple) {
  PatchMap map;
  map.AddEntry({1, 2}, 3, SHARED_BROTLI_ENCODING);
  map.AddEntry({3, 4}, 1, SHARED_BROTLI_ENCODING);
  map.AddEntry({5, 6}, 2, SHARED_BROTLI_ENCODING);
  map.AddEntry({7, 8}, 3, SHARED_BROTLI_ENCODING);
  map.AddEntry({9, 10}, 5, SHARED_BROTLI_ENCODING);

  ASSERT_EQ(map.RemoveEntries(3), PatchMap::MODIFIED_MAIN);

  PatchMap expected = {
      {{3, 4}, 1, SHARED_BROTLI_ENCODING},
      {{5, 6}, 2, SHARED_BROTLI_ENCODING},
      {{9, 10}, 5, SHARED_BROTLI_ENCODING},
  };

  ASSERT_EQ(map, expected);
}

TEST_F(PatchMapTest, RemoveEntries_NotFound) {
  PatchMap map;
  map.AddEntry({1, 2}, 3, SHARED_BROTLI_ENCODING);
  map.AddEntry({3, 4}, 1, SHARED_BROTLI_ENCODING);
  map.AddEntry({5, 6}, 2, SHARED_BROTLI_ENCODING);
  map.AddEntry({7, 8}, 3, SHARED_BROTLI_ENCODING);
  map.AddEntry({9, 10}, 5, SHARED_BROTLI_ENCODING);

  ASSERT_EQ(map.RemoveEntries(7), PatchMap::MODIFIED_NEITHER);

  PatchMap expected = {
      {{1, 2}, 3, SHARED_BROTLI_ENCODING},  {{3, 4}, 1, SHARED_BROTLI_ENCODING},
      {{5, 6}, 2, SHARED_BROTLI_ENCODING},  {{7, 8}, 3, SHARED_BROTLI_ENCODING},
      {{9, 10}, 5, SHARED_BROTLI_ENCODING},
  };

  ASSERT_EQ(map, expected);
}

TEST_F(PatchMapTest, RemoveEntries_Extension) {
  PatchMap map;
  map.AddEntry({1, 2}, 3, SHARED_BROTLI_ENCODING);
  map.AddEntry({3, 4}, 1, SHARED_BROTLI_ENCODING);
  map.AddEntry({5, 6}, 2, SHARED_BROTLI_ENCODING);
  map.AddEntry({7, 8}, 3, SHARED_BROTLI_ENCODING, true);
  map.AddEntry({9, 10}, 5, SHARED_BROTLI_ENCODING, true);

  ASSERT_EQ(map.RemoveEntries(5), PatchMap::MODIFIED_EXTENSION);

  PatchMap expected = {
      {{1, 2}, 3, SHARED_BROTLI_ENCODING},
      {{3, 4}, 1, SHARED_BROTLI_ENCODING},
      {{5, 6}, 2, SHARED_BROTLI_ENCODING},
      {{7, 8}, 3, SHARED_BROTLI_ENCODING, true},
  };

  ASSERT_EQ(map, expected);

  ASSERT_EQ(map.RemoveEntries(3), PatchMap::MODIFIED_BOTH);

  expected = {
      {{3, 4}, 1, SHARED_BROTLI_ENCODING},
      {{5, 6}, 2, SHARED_BROTLI_ENCODING},
  };

  ASSERT_EQ(map, expected);
}

TEST_F(PatchMapTest, RemovePatches_All) {
  auto map = PatchMap::FromProto(sample);
  ASSERT_TRUE(map.ok()) << map.status();

  ASSERT_EQ(map->RemoveEntries(1), PatchMap::MODIFIED_MAIN);
  ASSERT_EQ(map->RemoveEntries(2), PatchMap::MODIFIED_MAIN);

  PatchMap expected = {};
  ASSERT_EQ(*map, expected);
}

TEST_F(PatchMapTest, AddToProto) {
  PatchMap map = {
      {{23, 25, 28}, 0, SHARED_BROTLI_ENCODING},
      {{25, 28, 37}, 1, SHARED_BROTLI_ENCODING},
      {{30, 31}, 2, SHARED_BROTLI_ENCODING},
  };

  IFT expected;
  expected.set_default_patch_encoding(SHARED_BROTLI_ENCODING);

  auto* m = expected.add_subset_mapping();
  hb_set_unique_ptr set = make_hb_set(3, 0, 2, 5);
  m->set_bias(23);
  m->set_codepoint_set(SparseBitSet::Encode(*set.get()));
  m->set_id_delta(-1);

  m = expected.add_subset_mapping();
  set = make_hb_set(3, 0, 3, 12);
  m->set_bias(25);
  m->set_codepoint_set(SparseBitSet::Encode(*set.get()));
  m->set_id_delta(0);

  m = expected.add_subset_mapping();
  set = make_hb_set(2, 0, 1);
  m->set_bias(30);
  m->set_codepoint_set(SparseBitSet::Encode(*set.get()));
  m->set_id_delta(0);

  IFT proto;
  proto.set_default_patch_encoding(SHARED_BROTLI_ENCODING);
  map.AddToProto(proto);

  ASSERT_TRUE(MessageDifferencer::Equals(expected, proto))
      << Diff(expected, proto);
}

TEST_F(PatchMapTest, AddToProto_ExtensionFilter) {
  PatchMap map = {
      {{23, 25, 28}, 0, SHARED_BROTLI_ENCODING},
      {{25, 28, 37}, 1, SHARED_BROTLI_ENCODING},
      {{30, 31}, 2, SHARED_BROTLI_ENCODING, true},
  };

  IFT expected_1;
  IFT expected_2;
  expected_1.set_default_patch_encoding(SHARED_BROTLI_ENCODING);
  expected_2.set_default_patch_encoding(SHARED_BROTLI_ENCODING);

  auto* m = expected_1.add_subset_mapping();
  hb_set_unique_ptr set = make_hb_set(3, 0, 2, 5);
  m->set_bias(23);
  m->set_codepoint_set(SparseBitSet::Encode(*set.get()));
  m->set_id_delta(-1);

  m = expected_1.add_subset_mapping();
  set = make_hb_set(3, 0, 3, 12);
  m->set_bias(25);
  m->set_codepoint_set(SparseBitSet::Encode(*set.get()));
  m->set_id_delta(0);

  m = expected_2.add_subset_mapping();
  set = make_hb_set(2, 0, 1);
  m->set_bias(30);
  m->set_codepoint_set(SparseBitSet::Encode(*set.get()));
  m->set_id_delta(1);

  IFT proto_1;
  proto_1.set_default_patch_encoding(SHARED_BROTLI_ENCODING);
  map.AddToProto(proto_1);

  IFT proto_2;
  proto_2.set_default_patch_encoding(SHARED_BROTLI_ENCODING);
  map.AddToProto(proto_2, true);

  ASSERT_TRUE(MessageDifferencer::Equals(expected_1, proto_1))
      << Diff(expected_1, proto_1);
  ASSERT_TRUE(MessageDifferencer::Equals(expected_2, proto_2))
      << Diff(expected_2, proto_2);
}

TEST_F(PatchMapTest, AddToProto_ComplexIds) {
  PatchMap map = {
      {{23, 25, 28}, 0, SHARED_BROTLI_ENCODING},
      {{25, 28, 37}, 5, SHARED_BROTLI_ENCODING},
      {{30, 31}, 2, IFTB_ENCODING},
      {{}, 4, SHARED_BROTLI_ENCODING},
  };

  IFT expected;
  expected.set_default_patch_encoding(SHARED_BROTLI_ENCODING);

  auto* m = expected.add_subset_mapping();
  hb_set_unique_ptr set = make_hb_set(3, 0, 2, 5);
  m->set_bias(23);
  m->set_codepoint_set(SparseBitSet::Encode(*set.get()));
  m->set_id_delta(-1);

  m = expected.add_subset_mapping();
  set = make_hb_set(3, 0, 3, 12);
  m->set_bias(25);
  m->set_codepoint_set(SparseBitSet::Encode(*set.get()));
  m->set_id_delta(4);

  m = expected.add_subset_mapping();
  set = make_hb_set(2, 0, 1);
  m->set_bias(30);
  m->set_codepoint_set(SparseBitSet::Encode(*set.get()));
  m->set_id_delta(-4);
  m->set_patch_encoding(IFTB_ENCODING);

  m = expected.add_subset_mapping();
  m->set_id_delta(1);

  IFT proto;
  proto.set_default_patch_encoding(SHARED_BROTLI_ENCODING);
  map.AddToProto(proto);

  ASSERT_TRUE(MessageDifferencer::Equals(expected, proto))
      << Diff(expected, proto);
}

TEST_F(PatchMapTest, IsDependent) {
  ASSERT_FALSE(PatchMap::Entry({}, 0, IFTB_ENCODING).IsDependent());
  ASSERT_TRUE(PatchMap::Entry({}, 0, SHARED_BROTLI_ENCODING).IsDependent());
  ASSERT_TRUE(
      PatchMap::Entry({}, 0, PER_TABLE_SHARED_BROTLI_ENCODING).IsDependent());
}

}  // namespace ift::proto
