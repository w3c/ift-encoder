#include "ift/feature_registry/feature_registry.h"

#include "gtest/gtest.h"

namespace ift::feature_registry {

class FeatureRegistryTest : public ::testing::Test {
 protected:
  FeatureRegistryTest() {}
};

TEST_F(FeatureRegistryTest, DefaultFeatureTags) {
  // Should match the feature registry in the specification found here:
  // https://w3c.github.io/IFT/Overview.html#feature-tag-list
  //
  // Spot check a few entries to make sure things seem to line up.
  ASSERT_TRUE(DefaultFeatureTags().contains(HB_TAG('f', 'r', 'a', 'c')));
  ASSERT_TRUE(DefaultFeatureTags().contains(HB_TAG('v', 'a', 't', 'u')));
  ASSERT_TRUE(DefaultFeatureTags().contains(HB_TAG('v', 'r', 't', 'r')));

  ASSERT_FALSE(DefaultFeatureTags().contains(HB_TAG('f', 'w', 'i', 'd')));
  ASSERT_FALSE(DefaultFeatureTags().contains(HB_TAG('z', 'e', 'r', 'o')));

  ASSERT_FALSE(DefaultFeatureTags().contains(HB_TAG('c', 'v', '0', '1')));
  ASSERT_FALSE(DefaultFeatureTags().contains(HB_TAG('c', 'v', '5', '8')));
  ASSERT_FALSE(DefaultFeatureTags().contains(HB_TAG('c', 'v', '9', '9')));

  ASSERT_GT(DefaultFeatureTags().size(), 10);
}

}  // namespace ift::feature_registry
