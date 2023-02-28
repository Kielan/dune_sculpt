#include "intern/builder/dgraph_builder_api.h"

#include "testing/testing.h"

namespace dune::deg::tests {

class TestableApiNodeQuery : public ApiNodeQuery {
 public:
  static bool contains(const char *prop_id, const char *api_path_component)
  {
    return ApiNodeQuery::contains(prop_id, api_path_component);
  }
};

TEST(dgraph_builder_api, contains)
{
  EXPECT_TRUE(TestableApiNodeQuery::contains("location", "location"));
  EXPECT_TRUE(TestableApiNodeQuery::contains("location.x", "location"));
  EXPECT_TRUE(TestableApiNodeQuery::contains("pose.bone[\"blork\"].location", "location"));
  EXPECT_TRUE(TestableApiNodeQuery::contains("pose.bone[\"blork\"].location.x", "location"));
  EXPECT_TRUE(TestableApiNodeQuery::contains("pose.bone[\"blork\"].location[0]", "location"));

  EXPECT_FALSE(TestableApiNodeQuery::contains("", "location"));
  EXPECT_FALSE(TestableApiNodeQuery::contains("locatio", "location"));
  EXPECT_FALSE(TestableApiNodeQuery::contains("locationnn", "location"));
  EXPECT_FALSE(TestableApiNodeQuery::contains("test_location", "location"));
  EXPECT_FALSE(TestableApiNodeQuery::contains("location_test", "location"));
  EXPECT_FALSE(TestableApiNodeQuery::contains("test_location_test", "location"));
  EXPECT_FALSE(TestableApiNodeQuery::contains("pose.bone[\"location\"].scale", "location"));
  EXPECT_FALSE(TestableApiNodeQuery::contains("pose.bone[\"location\"].scale[0]", "location"));
}

}  // namespace dune::deg::tests
