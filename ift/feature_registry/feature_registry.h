#ifndef IFT_FEATURE_REGISTRY_FEATURE_REGISTRY_H_
#define IFT_FEATURE_REGISTRY_FEATURE_REGISTRY_H_

#include "hb.h"
#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_set.h"

namespace ift::featurge_registry {

static const absl::flat_hash_set<hb_tag_t>& DefaultFeatureTags() {
  static const absl::NoDestructor<absl::flat_hash_set<hb_tag_t>> kDefaultFeatures((absl::flat_hash_set<hb_tag_t>) {
    HB_TAG('a', 'b', 'v', 'f'),
    HB_TAG('a', 'b', 'v', 'm'),
    HB_TAG('a', 'b', 'v', 's'),
    HB_TAG('a', 'k', 'h', 'n'),
    HB_TAG('b', 'l', 'w', 'f'),
    HB_TAG('b', 'l', 'w', 'm'),
    HB_TAG('b', 'l', 'w', 's'),
    HB_TAG('c', 'a', 'l', 't'),
    HB_TAG('c', 'c', 'm', 'p'),
    HB_TAG('c', 'f', 'a', 'r'),
    HB_TAG('c', 'h', 'w', 's'),
    HB_TAG('c', 'j', 'c', 't'),
    HB_TAG('c', 'l', 'i', 'g'),
    HB_TAG('c', 's', 'w', 'h'),
    HB_TAG('c', 'u', 'r', 's'),
    HB_TAG('d', 'i', 's', 't'),
    HB_TAG('d', 'n', 'o', 'm'),
    HB_TAG('d', 't', 'l', 's'),
    HB_TAG('f', 'i', 'n', '2'),
    HB_TAG('f', 'i', 'n', '3'),
    HB_TAG('f', 'i', 'n', 'a'),
    HB_TAG('f', 'l', 'a', 'c'),
    HB_TAG('f', 'r', 'a', 'c'),
    HB_TAG('h', 'a', 'l', 'f'),
    HB_TAG('h', 'a', 'l', 'n'),
    HB_TAG('i', 'n', 'i', 't'),
    HB_TAG('i', 's', 'o', 'l'),
    HB_TAG('j', 'a', 'l', 't'),
    HB_TAG('k', 'e', 'r', 'n'),
    HB_TAG('l', 'i', 'g', 'a'),
    HB_TAG('l', 'j', 'm', 'o'),
    HB_TAG('l', 'o', 'c', 'l'),
    HB_TAG('l', 't', 'r', 'a'),
    HB_TAG('l', 't', 'r', 'm'),
    HB_TAG('m', 'a', 'r', 'k'),
    HB_TAG('m', 'e', 'd', '2'),
    HB_TAG('m', 'e', 'd', 'i'),
    HB_TAG('m', 'k', 'm', 'k'),
    HB_TAG('m', 's', 'e', 't'),
    HB_TAG('n', 'u', 'k', 't'),
    HB_TAG('n', 'u', 'm', 'r'),
    HB_TAG('p', 'r', 'e', 'f'),
    HB_TAG('p', 'r', 'e', 's'),
    HB_TAG('p', 's', 't', 'f'),
    HB_TAG('p', 's', 't', 's'),
    HB_TAG('r', 'a', 'n', 'd'),
    HB_TAG('r', 'c', 'l', 't'),
    HB_TAG('r', 'k', 'r', 'f'),
    HB_TAG('r', 'l', 'i', 'g'),
    HB_TAG('r', 'p', 'h', 'f'),
    HB_TAG('r', 't', 'l', 'a'),
    HB_TAG('r', 't', 'l', 'm'),
    HB_TAG('r', 'v', 'r', 'n'),
    HB_TAG('s', 's', 't', 'y'),
    HB_TAG('s', 't', 'c', 'h'),
    HB_TAG('t', 'j', 'm', 'o'),
    HB_TAG('v', 'a', 'l', 't'),
    HB_TAG('v', 'a', 't', 'u'),
    HB_TAG('v', 'c', 'h', 'w'),
    HB_TAG('v', 'e', 'r', 't'),
    HB_TAG('v', 'j', 'm', 'o'),
    HB_TAG('v', 'k', 'r', 'n'),
    HB_TAG('v', 'p', 'a', 'l'),
    HB_TAG('v', 'r', 't', '2'),
    HB_TAG('v', 'r', 't', 'r'),
  });
  return *kDefaultFeatures;
}

}  // namespace ift::feature_registry
#endif  // IFT_FEATURE_REGISTRY_FEATURE_REGISTRY_H_

