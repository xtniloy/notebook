# Benchmarks run nightly with release flags (preset `bench`):
#
#   cmake --preset bench && cmake --build --preset bench && ctest --preset bench
#
# Add one with: notes_add_test(NAME bench_x SOURCES bench_x.cpp LABELS bench)

# TC-PERF-001: note list scrolls 10,000 notes at < 16 ms per frame.
notes_add_test(NAME bench_notelistscroll SOURCES bench_notelistscroll.cpp LABELS bench
               LIBS notesapp_ui)
