/* *****************************************************************************
Copyright (c) 2018 Stan Chlebicki

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
***************************************************************************** */

#include <gtest/gtest.h>

#include <chrono>  // NOLINT() CPP11_INCLUDES
#include <vector>

#include <cxxmidi/file.hpp>
#include <cxxmidi/note.hpp>
#include <cxxmidi/output/null.hpp>
#include <cxxmidi/player/player_sync.hpp>

TEST(PlayerSync, CurrentTrackDuringCallback) {
  cxxmidi::File file;

  // All deltatimes are 0, so PlayerSync drains track 0 completely before
  // moving on to track 1 (TrackPending() breaks ties toward the lower
  // index), giving a fully deterministic event order to assert on.
  cxxmidi::Track &track0 = file.AddTrack();
  track0.push_back(cxxmidi::Event(0, cxxmidi::Message::kNoteOn,
                                  cxxmidi::Note::MiddleC(), 100));
  track0.push_back(cxxmidi::Event(0, cxxmidi::Message::kNoteOn,
                                  cxxmidi::Note::MiddleC(), 0));
  track0.push_back(
      cxxmidi::Event(0, cxxmidi::Message::kMeta, cxxmidi::Message::kEndOfTrack));

  cxxmidi::Track &track1 = file.AddTrack();
  track1.push_back(cxxmidi::Event(0, cxxmidi::Message::kNoteOn,
                                  cxxmidi::Note::MiddleC() + 2, 100));
  track1.push_back(cxxmidi::Event(0, cxxmidi::Message::kNoteOn,
                                  cxxmidi::Note::MiddleC() + 2, 0));
  track1.push_back(
      cxxmidi::Event(0, cxxmidi::Message::kMeta, cxxmidi::Message::kEndOfTrack));

  cxxmidi::output::Null output;
  cxxmidi::player::PlayerSync player(&output);
  player.SetFile(&file);

  std::vector<size_t> observed_tracks;
  player.SetCallbackEvent([&](cxxmidi::Event & /*event*/) {
    observed_tracks.push_back(player.CurrentTrack());
    return true;
  });

  player.Play();

  const std::vector<size_t> expected = {0, 0, 0, 1, 1, 1};
  EXPECT_EQ(observed_tracks, expected);
}

TEST(PlayerSync, StopDuringLongGapReturnsPromptly) {
  cxxmidi::File file;

  // Default tempo (500000 us/quarternote) and time division (500
  // ticks/quarternote) give 1000us/tick, so dt=150 is a 150ms gap before
  // the first event fires -- 15 chunks of PlayerLoop()'s ~10ms heartbeat.
  cxxmidi::Track &track0 = file.AddTrack();
  track0.push_back(cxxmidi::Event(150, cxxmidi::Message::kNoteOn,
                                  cxxmidi::Note::MiddleC(), 100));
  track0.push_back(
      cxxmidi::Event(0, cxxmidi::Message::kMeta, cxxmidi::Message::kEndOfTrack));

  cxxmidi::output::Null output;
  cxxmidi::player::PlayerSync player(&output);
  player.SetFile(&file);

  int heartbeats = 0;
  std::vector<int> observed_events;
  player.SetCallbackHeartbeat([&]() {
    heartbeats++;
    if (heartbeats == 2) player.Stop();
  });
  player.SetCallbackEvent([&](cxxmidi::Event & /*event*/) {
    observed_events.push_back(1);
    return true;
  });

  const auto start = std::chrono::steady_clock::now();
  player.Play();
  const auto elapsed = std::chrono::steady_clock::now() - start;

  // Stop() lands during the 3rd ~10ms chunk of the 150ms gap. Without the
  // fix, PlayerLoop() sleeps out the rest of the gap regardless (~150ms);
  // with the fix it unwinds within about one more chunk.
  EXPECT_LT(elapsed, std::chrono::milliseconds(75));
  EXPECT_TRUE(observed_events.empty());
}
