#ifndef BROTLI_HMTX_DIFFER_H_
#define BROTLI_HMTX_DIFFER_H_

#include "brotli/table_differ.h"
#include "ift/common/font_data.h"

namespace brotli {

class HmtxDiffer : public TableDiffer {
 private:
  enum Mode {
    INIT = 0,
    NEW_DATA,
    EXISTING_DATA,
  } mode = INIT;

  unsigned base_number_of_metrics;
  unsigned derived_number_of_metrics;

 public:
  HmtxDiffer(ift::common::FontData base_hhea,
             ift::common::FontData derived_hhea)
      : base_number_of_metrics(GetNumberOfMetrics(base_hhea)),
        derived_number_of_metrics(GetNumberOfMetrics(derived_hhea)) {}

  void Process(unsigned derived_gid, unsigned base_gid,
               unsigned base_derived_gid, bool is_base_empty,
               unsigned* base_delta, /* OUT */
               unsigned* derived_delta /* OUT */) override {
    mode = NEW_DATA;
    *derived_delta = derived_gid < derived_number_of_metrics ? 4 : 2;
    *base_delta = 0;

    if (derived_gid == base_derived_gid) {
      if ((derived_gid < derived_number_of_metrics &&
           base_gid < base_number_of_metrics) ||
          (derived_gid >= derived_number_of_metrics &&
           base_gid >= base_number_of_metrics)) {
        // Can only copy existing data if both base and derived are on the same
        // side of number of metrics.
        mode = EXISTING_DATA;
        *base_delta = base_gid < base_number_of_metrics ? 4 : 2;
      }
    }

    if (mode == NEW_DATA && is_base_empty) {
      *base_delta = base_gid < base_number_of_metrics ? 4 : 2;
    }
  }

  void Finalize(unsigned* base_delta, /* OUT */
                unsigned* derived_delta /* OUT */) const override {
    // noop
    *base_delta = 0;
    *derived_delta = 0;
  }

  bool IsNewData() const override { return mode == NEW_DATA; }

 private:
  static unsigned GetNumberOfMetrics(const ift::common::FontData& hhea) {
    constexpr unsigned field_offset = 34;

    if (hhea.size() < field_offset + 2) {
      return 0;
    }

    const uint8_t* num_metrics =
        hhea.span().data() + field_offset;
    return (num_metrics[0] << 8) | num_metrics[1];
  }
};

}  // namespace brotli

#endif  // BROTLI_HMTX_DIFFER_H_
