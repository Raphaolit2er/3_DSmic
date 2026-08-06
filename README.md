DS Mic: A tool that turns your Nintendo DS microphone into a wireless PC microphone over local Wi-Fi.

What You Need:
- A Nintendo DS flashcart or a DS emulator that supports Wi-Fi simulation (such as MelonDS or No$GBA).
- A computer to run the Python code
- Python 3.8 or higher (be careful as 3.14 doesn't have pyaudio at the time of posting this)
- Run `pip install Pillow pyaudio`
- Install a virtual cable so the streamed audio routes smoothly into Discord, Zoom, or other apps as a virtual microphone input.

How to Run:
1. Open the Python server script file.
2. Select your audio quality, enter your virtual cable input device (VB-Audio Virtual Cable has "CABLE input" for example), and enter your virtual cable output device (VB-Audio Virtual Cable has "CABLE output") in the GUI.
3. Click the "Start Server" button. The server will listen via UDP on your selected port.
4. Boot the compiled client "ds-mic-client.nds" file on your DS hardware or emulator.
5. Press the D-pad to enter your host PC's local IP address, then type the port number using the numeric on-screen keyboard.
6. Once connected, your DS microphone stream will stream live to your PC, showing real-time amplitude levels in the visualizer!

Disclaimers:
- Only 1 console can be connected at a time.
- Memory leaks are a possibility.
- This project was made using AIs, as I am not a good enough coder to do that on my own. Sorry to people who thought it was made by hand.
