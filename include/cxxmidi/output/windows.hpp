/* *****************************************************************************
RtMidi: realtime MIDI i/o C++ classes
Copyright (c) 2003-2011 Gary P. Scavone

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation files
(the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge,
publish, distribute, sublicense, and/or sell copies of the Software,
and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

Any person wishing to distribute modifications to the Software is
requested to send the modifications to the original developer so that
they can be incorporated into the canonical version.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
***************************************************************************** */

#ifndef INCLUDE_CXXMIDI_OUTPUT_WINDOWS_HPP_
#define INCLUDE_CXXMIDI_OUTPUT_WINDOWS_HPP_

#include <mmsystem.h>
#include <windows.h>

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <cxxmidi/output/abstract.hpp>

namespace cxxmidi {
namespace output {

class Windows : public Output::Abstract {
  static const int RT_SYSEX_BUFFER_SIZE = 1024;
  static const int RT_SYSEX_BUFFER_COUNT = 4;

  // A structure to hold variables related to the CoreMIDI API
  // implementation.
  struct WinMidiData {
    HMIDIIN inHandle;    // Handle to Midi Input Device
    HMIDIOUT outHandle;  // Handle to Midi Output Device
    DWORD lastTime;
    LPMIDIHDR sysexBuffer[RT_SYSEX_BUFFER_COUNT];
  };

 public:
  inline Windows();
  inline explicit Windows(unsigned int initialPort_);
  inline virtual ~Windows();

  Windows(const Windows &);             // non-copyable
  Windows &operator=(const Windows &);  // non-copyable (assignment)

  inline virtual void OpenPort(unsigned int portNumber_ = 0);
  inline virtual void ClosePort();
  inline virtual void OpenVirtualPort(
      const std::string &portName_ = std::string("CxxMidi Output"));
  inline virtual size_t GetPortCount();
  inline virtual std::string GetPortName(unsigned int portNumber_ = 0);
  inline virtual void SendMessage(const std::vector<uint8_t> *msg_);

 protected:
  inline virtual void Initialize();

 private:
  void *api_data_;
};

}  // namespace output
}  // namespace cxxmidi

namespace cxxmidi {
namespace Output {

Windows::Windows() { this->Initialize(); }

Windows::Windows(unsigned int initialPort_) {
  this->Initialize();
  this->OpenPort(initialPort_);
}

Windows::~Windows() {
  // Close a connection if it exists.
  this->ClosePort();

  // Cleanup
  WinMidiData *data = static_cast<WinMidiData *>(api_data_);
  delete data;
}

size_t Windows::GetPortCount() { return midiOutGetNumDevs(); }

std::string Windows::GetPortName(unsigned int portNumber_) {
  std::string stringName;
  unsigned int nDevices = midiOutGetNumDevs();
  if (portNumber_ >= nDevices) {
#ifndef CXXMIDI_QUIET
    std::cerr << "CxxMidi: invalid port number" << portNumber_ << std::endl;
#endif
    return stringName;
  }

  MIDIOUTCAPS deviceCaps;
  midiOutGetDevCaps(portNumber_, &deviceCaps, sizeof(MIDIOUTCAPS));

  // For some reason, we need to copy character by character with
  // UNICODE (thanks to Eduardo Coutinho!).
  // std::string stringName = std::string( deviceCaps.szPname );
  char nameString[MAXPNAMELEN];
  for (int i = 0; i < MAXPNAMELEN; ++i)
    nameString[i] = static_cast<char>(deviceCaps.szPname[i]);

  stringName = nameString;
  return stringName;
}

void Windows::Initialize() {
  // We'll issue a warning here if no devices are available but not
  // throw an error since the user can plug something in later.
  size_t nDevices = this->GetPortCount();
#ifndef CXXMIDI_QUIET
  if (nDevices == 0) std::cerr << "CxxMidi: no devices available" << std::endl;
#endif

  // Save our api-specific connection information.
  WinMidiData *data = new WinMidiData;
  api_data_ = reinterpret_cast<void *>(data);
}

void Windows::OpenPort(unsigned int portNumber_) {
  if (_connected) {
#ifndef CXXMIDI_QUIET
    std::cerr << "CxxMidi: a valid connection already exists" << std::endl;
#endif
    return;
  }

  size_t nDevices = this->GetPortCount();
#ifndef CXXMIDI_QUIET
  if (nDevices < 1)
    std::cerr << "CxxMidi: no MIDI output destinations found" << std::endl;
  if (portNumber_ >= nDevices)
    std::cerr << "CxxMidi: invalid port number " << portNumber_ << std::endl;
#endif

  WinMidiData *data = static_cast<WinMidiData *>(api_data_);
  MMRESULT result = midiOutOpen(&data->outHandle, portNumber_, (DWORD)NULL,
                                (DWORD)NULL, CALLBACK_NULL);
#ifndef CXXMIDI_QUIET
  if (result != MMSYSERR_NOERROR)
    std::cerr << "CxxMidi: Windows MM output port init error" << std::endl;
#endif

  _connected = true;
}

void Windows::ClosePort() {
  if (_connected) {
    WinMidiData *data = static_cast<WinMidiData *>(api_data_);
    midiOutReset(data->outHandle);
    midiOutClose(data->outHandle);
    _connected = false;
  }
}

void Windows::OpenVirtualPort(const std::string & /*portName_ */) {
  // This function cannot be implemented for the Windows MM MIDI API

#ifndef CXXMIDI_QUIET
  std::cerr << "CxxMidi: Windows MM MIDI virtual ports can't be implemented"
            << std::endl;
#endif
}

void Windows::SendMessage(const std::vector<uint8_t> *msg_) {
  unsigned int nBytes = static_cast<unsigned int>(msg_->size());
  if (nBytes == 0) {
#ifndef CXXMIDI_QUIET
    std::cerr << "CxxMidi: message argument is empty" << std::endl;
#endif
    return;
  }

  MMRESULT result;
  WinMidiData *data = static_cast<WinMidiData *>(api_data_);
  if (msg_->at(0) == 0xF0) {  // Sysex message
    // Allocate buffer for sysex data.
    char *buffer = reinterpret_cast<char *>(malloc(nBytes));
#ifndef CXXMIDI_QUIET
    if (buffer == NULL)
      std::cerr << "CxxMidi: message malloc error " << std::endl;
#endif

    // Copy data to buffer
    for (unsigned int i = 0; i < nBytes; ++i) buffer[i] = msg_->at(i);

    // Create and prepare MIDIHDR structure
    MIDIHDR sysex;
    sysex.lpData = (LPSTR)buffer;
    sysex.dwBufferLength = nBytes;
    sysex.dwFlags = 0;
    result = midiOutPrepareHeader(data->outHandle, &sysex, sizeof(MIDIHDR));
    if (result != MMSYSERR_NOERROR) {
      free(buffer);
#ifndef CXXMIDI_QUIET
      std::cerr << "CxxMidi: error preparing sysex header" << std::endl;
#endif
    }

    // Send the message.
    result = midiOutLongMsg(data->outHandle, &sysex, sizeof(MIDIHDR));
    if (result != MMSYSERR_NOERROR) {
      free(buffer);
#ifndef CXXMIDI_QUIET
      std::cerr << "CxxMidi: error sending sysex message" << std::endl;
#endif
    }

    // Unprepare the buffer and MIDIHDR
    while (MIDIERR_STILLPLAYING ==
           midiOutUnprepareHeader(data->outHandle, &sysex, sizeof(MIDIHDR)))
      Sleep(1);
    free(buffer);
  } else {  // Channel or system message
    // Make sure the message size isn't too big
    if (nBytes > 3) {
#ifndef CXXMIDI_QUIET
      std::cerr
          << "CxxMidi: message size is greater than 3 bytes (and not sysex)"
          << std::endl;
#endif
      return;
    }

    // Pack MIDI bytes into double word
    DWORD packet;
    unsigned char *ptr = (unsigned char *)&packet;
    for (unsigned int i = 0; i < nBytes; ++i) {
      *ptr = msg_->at(i);
      ++ptr;
    }

    // Send the message immediately
    result = midiOutShortMsg(data->outHandle, packet);
#ifndef CXXMIDI_QUIET
    if (result != MMSYSERR_NOERROR)
      std::cerr << "CxxMidi: error sending MIDI message" << std::endl;
#endif
  }
}

}  // namespace Output
}  // namespace cxxmidi

#endif  // INCLUDE_CXXMIDI_OUTPUT_WINDOWS_HPP_
