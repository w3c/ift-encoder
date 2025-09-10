import csv
import re
import sys

def print_set():
  with open(sys.argv[1]) as r:
    reader = csv.reader(r, delimiter=",")
    lines = [row for row in reader]

    i = 1
    for row in lines:
      if row[0] == "Tag" or row[0].startswith("#"):
        continue

      if int(row[2]) != 1:
        continue

      m = re.search("[a-z]{2}([0-9]{2})-[a-z]{2}([0-9]{2})", row[0])
      if m:
        start = i
        end = i + (int(m.group(2)) - int(m.group(1)))
        i += end - start + 1
      else:
        start = i
        end = i
        i += 1

      for v in range(start, end + 1):
        if start == end:
          tag_str = f"HB_TAG('{row[0][0]}', '{row[0][1]}', '{row[0][2]}', '{row[0][3]}')"
        else:
          tag_str = f"HB_TAG('{row[0][0]}', '{row[0][1]}', '{v_str[0]}', '{v_str[1]}')"

        print(f"    {tag_str},")


print("#ifndef IFT_FEATURE_REGISTRY_FEATURE_REGISTRY_H_")
print("#define IFT_FEATURE_REGISTRY_FEATURE_REGISTRY_H_")
print("")
print("#include \"hb.h\"")
print("#include \"absl/base/no_destructor.h\"")
print("#include \"absl/container/flat_hash_set.h\"")
print("")
print("namespace ift::feature_registry {")
print("")
print("static const absl::flat_hash_set<hb_tag_t>& DefaultFeatureTags() {")
print("  static const absl::NoDestructor<absl::flat_hash_set<hb_tag_t>> kDefaultFeatures((absl::flat_hash_set<hb_tag_t>) {")
print_set()
print("  });")
print("  return *kDefaultFeatures;")
print("}")
print("")
print("}  // namespace ift::feature_registry")
print("#endif  // IFT_FEATURE_REGISTRY_FEATURE_REGISTRY_H_")
