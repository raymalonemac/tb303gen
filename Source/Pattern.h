#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include "Seed.h"
#include "MidiImport.h"

namespace tb
{
inline juce::StringArray scaleNames()
{
    return { "Minor pentatonic", "Phrygian", "Natural minor", "Phrygian dominant", "Harmonic minor", "Custom" };
}

inline juce::String noteName (int midiNote)
{
    return juce::MidiMessage::getMidiNoteName (midiNote, true, true, 4);   // 36 -> C2
}

// bass only (channel 1) - never any drums
inline bool writeMidiFile (const Pattern& pat, double bpm, const juce::File& file)
{
    constexpr int ppq = 480, stepT = ppq / 4, gateT = (int) (stepT * 0.55), overlapT = 12;

    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, "BASSLINER303 Bassline"), 0.0);
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) (60000000.0 / juce::jmax (1.0, bpm))), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);

    for (int i = 0; i < pat.len; ++i)
    {
        const Step& s = pat[(size_t) i];
        if (! s.active) continue;
        const Step* nxt = (i + 1 < pat.len) ? &pat[(size_t) (i + 1)] : nullptr;

        const int start = i * stepT;
        int end = start + gateT;
        if (s.slide && nxt && nxt->active)
            end = start + stepT + (nxt->pitch != s.pitch ? overlapT : 0);

        seq.addEvent (juce::MidiMessage::noteOn (1, (int) s.pitch, (juce::uint8) (s.accent ? kVelAccent : kVelNormal)), (double) start);
        seq.addEvent (juce::MidiMessage::noteOff (1, (int) s.pitch), (double) end);
    }
    seq.addEvent (juce::MidiMessage::endOfTrack(), (double) (pat.len * stepT));    // exact loop length
    seq.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppq);
    mf.addTrack (seq);

    file.deleteFile();
    juce::FileOutputStream os (file);
    return os.openedOk() && mf.writeTo (os);
}

struct LoadedMidi { Imported imp; bool ok = false; juce::String error; };

// reads any standard .mid and turns it into a one-line pattern (see MidiImport.h)
inline LoadedMidi readMidiFile (const juce::File& file)
{
    LoadedMidi out;
    juce::FileInputStream in (file);
    juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in))
    {
        out.error = "That does not look like a MIDI file.";
        return out;
    }

    const int division = mf.getTimeFormat();
    if (division <= 0)
    {
        out.error = "SMPTE-timed MIDI files are not supported.";
        return out;
    }

    std::vector<RawNote> notes;
    double bpm = 0.0;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (t));
        seq.updateMatchedPairs();
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            auto* h = seq.getEventPointer (i);
            const auto& m = h->message;
            if (m.isTempoMetaEvent() && bpm <= 0.0)
                bpm = 60.0 / juce::jmax (1.0e-6, m.getTempoSecondsPerQuarterNote());
            if (! m.isNoteOn()) continue;

            const double s = m.getTimeStamp();
            const double e = h->noteOffObject != nullptr ? h->noteOffObject->message.getTimeStamp() : s + division * 0.25;
            notes.push_back ({ s, e, m.getNoteNumber(), (int) m.getVelocity(), m.getChannel() - 1 });
        }
    }

    out.imp = notesToPattern (notes, (double) division, bpm);
    out.ok = out.imp.ok;
    out.error = out.imp.error;
    return out;
}
} // namespace tb
