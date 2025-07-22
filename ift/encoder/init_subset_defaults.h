#ifndef IFT_ENCODER_INIT_SUBSET_DEFAULTS_H_
#define IFT_ENCODER_INIT_SUBSET_DEFAULTS_H_

#include "ift/encoder/subset_definition.h"
#include "ift/feature_registry/feature_registry.h"

using ift::feature_registry::DefaultFeatureTags;

namespace ift::encoder {

// Configures a subset definition to contain all of the default, always included
// items (eg. https://w3c.github.io/IFT/Overview.html#feature-tag-list)
static void AddInitSubsetDefaults(SubsetDefinition& subset_definition) {
  std::copy(DefaultFeatureTags().begin(), DefaultFeatureTags().end(),
            std::inserter(subset_definition.feature_tags,
                          subset_definition.feature_tags.begin()));
}

}  // namespace ift::encoder

#endif  // IFT_ENCODER_INIT_SUBSET_DEFAULTS_H_