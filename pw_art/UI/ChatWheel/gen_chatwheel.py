"""Chat wheel textures (same look as the ping wheel): wheels for 4..8 sectors, see ../PingWheel/wheel_gen.py.

  python gen_chatwheel.py <out dir>     (game: pw/branches/r1117/Data/UI/Styles/Icons/ChatWheel)

chat_wheel_bg_<N>.dds 512x512 (wheel radius 244, center circle 58), chat_wheel_hl_<N>_<i>.dds sector highlights;
the printed CHAT_HL_OFFSETS go to MainInterface.as.
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'PingWheel'))
from wheel_gen import wheel, as_offsets

offs = wheel(sys.argv[1], 'chat_wheel', 512, 244, 58, 0)
print(as_offsets('CHAT_HL_OFFSETS', offs))
