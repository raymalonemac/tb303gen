Core logic tests (no JUCE needed):
  g++ -std=c++17 -Wall -Wextra Tests/core_test.cpp -o core_test && ./core_test
Checks: seed size, 4..64 bars (first bars stay put when the BARS fader moves), .mid import, LFO maths.
