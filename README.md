MIDI Hardware Tracker-Style Sequencer

This repository contains the firmware and documentation for a hardware tracker-style MIDI sequencer designed for DJs and home producers. The sequencer provides an intuitive and tactile interface for creating and performing music, blending features of classic hardware sequencers with modern MIDI control.

Features

	•	16 Tracks
Each track consists of 4 bytes:
	•	1st byte: Type and channel
	•	2nd byte: Data byte 1 (Note)
	•	3rd byte: Data byte 2 (Velocity)
	•	4th byte: Delay
	•	Magnetic Encoder
	•	Controls track position and syncs playback “vinyl-style.”
	•	Includes a shift key beneath the encoder for alternate functions.
	•	Tempo Fader
	•	Adjusts playback tempo.
	•	Mode selection buttons (+8, +16, Wide) with LED indicators.
	•	16-Key Cherry MX Keypad
	•	Functions in Edit Mode (to be added) or Mute Mode for muting individual tracks.
	•	8-Key Cherry MX Keypad (2x9 Table)
Each key has multiple functions depending on click, shift, or hold:
	•	Click | Shift | Hold
	•	Cue | Play | NA
	•	A | Edit | NA
	•	B | Mute | NA
	•	Scene | New Scene | NA
	•	Nudge | Tempo | NA
	•	Copy | Paste | NA
	•	Delete | Undo | NA
	•	OK | Duplicate | NA
	•	8x32 7-Segment LED Display
	•	Large (0.28” per character).
	•	Displays 4 columns of data in Scene Mode:
	•	Original Tempo
	•	Scene
	•	A Track
	•	B Track
	•	Scene Mode
	•	Use the OK key to select and assign scenes to Track A or B.
	•	Build scenes from 16 editable tracks.

Photos

(Add images of the device and its components here)

License

This project is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License (CC BY-NC-SA 4.0).

Summary:

	•	You are free to:
	•	Build and modify the hardware and firmware for personal use.
	•	Share your modifications and designs as long as they comply with the license.
	•	Restrictions:
	•	Non-commercial use only. You may not use this project for commercial purposes.
	•	You must provide appropriate credit and distribute your contributions under the same license.

For details, see the full license text in the LICENSE file.