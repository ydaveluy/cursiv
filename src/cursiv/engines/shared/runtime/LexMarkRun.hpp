// Where the run of a mark's class begins, read back from the point its echo
// is looked for. Launches of one relay come in order along a run, so what an
// earlier one read is not read again.
#pragma once

#include "cursiv/core/text/TextOffset.hpp"
#include "cursiv/engines/shared/table/ByteSet.hpp"

namespace cursiv {

class LexMarkRun {
 public:
  // The start of the run of `cls` that ends at `end`, not before `start`.
  static TextOffset back(const ByteSet& cls, const char* src, TextOffset start,
                         TextOffset end) {
    while (end > start && cls.has(static_cast<unsigned char>(src[end - 1])))
      --end;
    return end;
  }

  // The same, from what the last call read: `[_begin, _end)` is a run of the
  // class, and the byte before `_begin` is not of it.
  TextOffset begin(const ByteSet& cls, const char* src, TextOffset start,
                   TextOffset end) {
    if (_known && end >= _begin) {
      if (end > _end) {
        if (const TextOffset b = back(cls, src, _end, end); b != _end)
          _begin = b;
        _end = end;
      }
      return _begin;
    }
    _begin = back(cls, src, start, end);
    _end = end;
    _known = true;
    return _begin;
  }

 private:
  TextOffset _begin = 0;
  TextOffset _end = 0;
  bool _known = false;
};

}  // namespace cursiv
