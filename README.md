well my friends, my files-mod is a C project for inspecting binary files, editing their bytes and rebuilding files
it starts with a language menu, so you can use Portuguese or English
the export option reads a file and creates a txt dump with its bytes in hexadecimal
the dump also stores the original file name and size, so you know what you are editing
you can modify the hexadecimal values in that txt file and then load it back into the program
the rebuild option validates the dump and creates a new file with the changes
the original file stays untouched, which is pretty useful for testing changes without losing anything
the inspect option shows the size, null bytes, printable bytes and ASCII text strings found
it also tries to identify formats like PNG, JPEG, ZIP, PDF, WAV and Windows executables
when it finds a PE file, the program shows basic information about the header and its sections
the compare option is there to check two files and show which byte positions are different
the program creates a folder called files-mod-output to store the results automatically
the code is split into modules, main handles the menu and the other files handle each function
to compile on Windows, well thats it, after that just compile the files and build the .exe to use it, hope you all enjoy the project, bye
