# Hardware Tracker MIDI Sequencer

A hardware-style MIDI sequencer designed for DJs and home producers. This device combines tactile control with powerful features to streamline music creation and performance.

---

## Features

### 🎵 Tracks & MIDI Control
- **16 Tracks**: Each track consists of 4 bytes:  
  - **Byte 1**: Type and channel  
  - **Byte 2**: Data byte 1 (note) 
  - **Byte 3**: Data byte 2 (velocity)
  - **Byte 4**: Delay  
- **Editing**: Tracks can be edited and muted via dedicated keypads.

### 🔄 Magnetic Encoder & Sync Control
- **Magnetic Encoder**:  
  - Control track position.  
  - Sync with song tempo "vinyl style".  
  - **Shift Key** under the encoder for alternate functions.  

### 🎚️ Tempo Control
- **Tempo Fader**: Fine-tune playback tempo.  
- **Tempo Modes**:  
  - Select tempo control range (+8, +16, wide) via buttons and LED indicators.

### ⌨️ Keypads & Modes
#### 16-Key Cherry MX Keypad
- **Modes**:  
  - Edit mode (upcoming functionality).  
  - Mute mode: Mute individual tracks.

#### 8-Key Cherry MX Keypad 
| Key        | Shift Function   | 
|------------|------------------|
| Cue        | Play             |
| A          | Edit             |
| B          | Mute             |
| Scene      | New Scene        |
| Nudge      | Tempo            |
| Copy       | Paste            |
| Delete     | Undo             |
| OK         | Duplicate        |

### 📟 Display
- **8x32 7-Segment LED Display**:  
  - Each character is 0.28".  
  - Displays key information across 4 columns in Scene Mode:  
    - Original tempo  
    - Scene  
    - A track  
    - B track  

### 🎛️ Scene Mode
- Use **OK Key** to select a scene and assign it to **Track A** or **Track B** via the **A** and **B** keys.  
- Each scene comprises 16 editable tracks.  
- Select and edit tracks using the **16-Key Keypad** in Edit Mode.  

---

## 📸 Photos
_Add photos here._  

---

## License
This project is licensed under the [Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License](https://creativecommons.org/licenses/by-nc-sa/4.0/).  

- You are free to build and modify the hardware and software for **non-commercial purposes**.  
- Commercial use of this project is strictly prohibited.  
