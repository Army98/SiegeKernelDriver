Hardcoded offsets. 
The loop for the ViewMatrix, and EntList positions in the main.cpp (user overlway) and main.c (driver) are communicating with the user overlays .data section. 
Theres a big ass array that sets a few thousand bytes to 2, turn off ASLR for the user overlay and youll find it. Or you could do some random nums and pattern scan it for automation idc lol i dont distribute.
The strides should be self explanatory, I was too lazy to optimize but this is only using a few hundred btyes as is. (byte 1 for x, 2 for y, 3 for z) (16 bytes for view matrix etc)
