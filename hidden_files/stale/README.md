# Stale: test_sg9_processor_headless.cpp

Archived 2026-10-08. This headless processor test was written for the OLD
70s-saw/mantra-bell/COSMOS engine and its old Sg9Params (v3_body, asc_run,
cosmos, respan, ...). It does not compile against the new web-engine port
and requires JUCE modules to build, which are not available in this Linux
environment.

The new engine is covered by tests/test_sg9.cpp (100 checks, green).
If a headless processor test is wanted again, rewrite it against the new
Sg9Params in dsp/Sg9Dsp.h and the new APVTS IDs in juce/Sg9Processor.cpp.
