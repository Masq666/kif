/* 
 * kif.h
 *
 * Kompakt Icon Format - A very simple RLE based icon format.
 *
 * Copyright (C) 1998 - 2023 Philipe Rubio. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
    -- BACKGROUND --

    The .kif format is a slight modification of an image format I developed for an old DOS GUI called 'Peephole' in the late 90s.
    Unfortunately, its code was lost decades ago, but I recently discovered some notes and other files on an old HDD,
    including specifications for the .kif format. Currently, I am working on a hobby operating system (http://tardi-os.org) and decided to 
    revive the old .kif format for icons since it is comparable in size to .png and much faster to decode, without any external dependencies required.

    As the original code was lost, I had to reimplement the encoder/decoder for the format. 
    I based some of my code on the QOI format code (https://qoiformat.org/), as it is spiritually similar.

    --- DESCRIPTION ---

    The Kompakt Icon Format is a very simplified RLE (Run length encoding) bitmap format.

    --- HOW TO USE ---

*/

#pragma pack(push, 1) // Disable padding
	typedef struct {
		uint32_t Magic;					// = 'kif1'
		uint8_t BPP;					// 3 = RGB, 4 = RGBA (24/32bit palette)
		uint8_t Compressed;				// Will at the moment be 0, as we have no compression yet.
		uint16_t palEntries;			// Number of palette entries (bpp * palEntries = bytes to read after header to get the palette. Limited to 65K unique colors.)		
		uint16_t Width;					// Width
		uint16_t Height;				// Heigth
		uint32_t RLEEntries;			// Number of 2 byte RLE encoded data entries (paletteID, Run Length)
	} KIFHeader;	//  Header is 16 bytes

	typedef union {
		struct { unsigned char r, g, b, a; } rgba;
		uint32_t v;
	} kif_rgba_t;

	typedef struct {
		unsigned char pID, rle;
	} kif_rle_t;
#pragma pack(pop) // Restore default packing

/* --- API Functions --- */
int kif_write(const char *Filename, const void *Data, KIFHeader *Header);
void *kif_read(const char *Filename, KIFHeader *Header, int OutputBPP);
void *kif_encode(const void *Data, KIFHeader *Header, int *OutputLength);
void *kif_decode(const void *RawData, KIFHeader *Header, int OutputBPP);

/* --- Internal Functions --- */
static unsigned short _read16bit(const unsigned char *buffer);
static unsigned int _read32bit(const unsigned char *buffer);

static void _generate_palette(const void *Data, KIFHeader *Header, kif_rgba_t *Palette, int *NumberOfColors);
static int _in_palette(kif_rgba_t Color, kif_rgba_t *Palette, int *NumberOfColors);

/**
 * 
*/
int kif_write(const char *Filename, const void *Data, KIFHeader *Header){
	FILE *OpenedFile = fopen(Filename, "wb");
	int Size;
	void *Encoded;

	if(!OpenedFile){
		return 0;
	}

	Encoded = kif_encode(Data, Header, &Size);

	if(!Encoded){
		fclose(OpenedFile);
		return 0;
	}

	fwrite(Encoded, 1, Size, OpenedFile);
	fclose(OpenedFile);

	free(Encoded);
	return Size;
}

/**
 *
*/
void *kif_read(const char *Filename, KIFHeader *Header, int OutputBPP){
	FILE *OpenedFile = fopen(Filename, "rb");
	int Size, BytesRead;
	void *Decoded, *Data;

	if(!OpenedFile){
		return 0;
	}

	fseek(OpenedFile, 0, SEEK_END);
	Size = ftell(OpenedFile);

	if(Size <= 0){
		fclose(OpenedFile);
		return 0;
	}

	fseek(OpenedFile, 0, SEEK_SET);
	Data = malloc(Size);

	if(!Data){
		fclose(OpenedFile);
		return 0;
	}

	BytesRead = fread(Data, 1, Size, OpenedFile);
	fclose(OpenedFile);

	Decoded = kif_decode(Data, Header, OutputBPP);
	free(Data);

	return Decoded;
}

/**
 * Decode a .kif icon
 * @param Data Pointer to input data
 * @param Header Pointer to a KIFHeader struct
 * @param OutputBPP Set output bits per pixel
 * @return void Returns a pointer to a buffer containing raw icon data (RGB or RGBA)
*/
void *kif_decode(const void *Data, KIFHeader *Header, int OutputBPP){
	if(Data == NULL || Header == NULL || (OutputBPP != 24 && OutputBPP != 32)){
		return NULL;
	}

	const unsigned char* data_bytes = (const unsigned char*)Data; // Cast data to unsigned char pointer

	// Fill the header struct
	Header->Magic = _read32bit(data_bytes);
	Header->BPP = data_bytes[4];
	Header->Compressed = data_bytes[5];
	Header->palEntries = _read16bit(data_bytes + 6);
	Header->Width = _read16bit(data_bytes + 8);
	Header->Height = _read16bit(data_bytes + 10);
	Header->RLEEntries = _read32bit(data_bytes + 12);

    // Allocate memory for palette data
    kif_rgba_t* Palette = (kif_rgba_t*)malloc(Header->palEntries * sizeof(kif_rgba_t));

    // Read palette data from data buffer
	for(int i = 0; i < Header->palEntries; i++){
		Palette[i].rgba.r = data_bytes[sizeof(KIFHeader) + (i * 4)];     // Read R component
		Palette[i].rgba.g = data_bytes[sizeof(KIFHeader) + (i * 4) + 1]; // Read G component
		Palette[i].rgba.b = data_bytes[sizeof(KIFHeader) + (i * 4) + 2]; // Read B component
		Palette[i].rgba.a = data_bytes[sizeof(KIFHeader) + (i * 4) + 3]; // Read A component
    }

	// Allocate memory for pixel buffer / decoded image
	unsigned char* Decoded;

	Decoded = (unsigned char*)malloc(Header->Width * Header->Height * 4);    // 4 Bytes per pixel

	int DecodedIndex = 0; // Index for writing to pixel buffer

    for(int i = 0; i < Header->RLEEntries; i++){
        unsigned char index = data_bytes[sizeof(KIFHeader) + Header->palEntries * 4 + i * 2];			// Read index from image data
        unsigned char RunLength = data_bytes[sizeof(KIFHeader) + Header->palEntries * 4 + i * 2 + 1];	// Read run length from image data

        for(int j = 0; j < RunLength; j++){
            // Copy RGBA values from palette to pixel buffer
            Decoded[DecodedIndex++] = Palette[index].rgba.r; // R component
            Decoded[DecodedIndex++] = Palette[index].rgba.g; // G component
            Decoded[DecodedIndex++] = Palette[index].rgba.b; // B component

            if(OutputBPP == 32){
                Decoded[DecodedIndex++] = Palette[index].rgba.a; // A component
            }
        }
    }

    // We no longer need the Palette.
    free(Palette);

    // Needs to be free()d after use.
    return Decoded;
}

/**
 * Encode raw image data into a .kif icon
 * @param data Pointer to input data (raw image data)
 * @param Header Pointer to a KIFHeader struct
 * @param OutputLength Pointer to an integer to store the output data length
 * @return void Returns a pointer to a buffer containing the encoded .kif icon data
 */
void *kif_encode(const void *Data, KIFHeader *Header, int *OutputLength){
    // Check for valid inputs
    if(Data == NULL || Header == NULL || OutputLength == NULL){
        return NULL;
    }

    int NumberOfColors = 0; // Number of colors in the palette

	// Create palette
	// TODO: The kif_rle_t stuct which holds the palette ID of the encoded data only supports 256 colors!!
	kif_rgba_t Palette[65536];	// Max number of palette entries is 65536 (16-bit int)

    // Generate palette of unique RGBA colors
    _generate_palette(Data, Header, Palette, &NumberOfColors);

    // Copy number of palette entries to header
    Header->palEntries = NumberOfColors;

    // Allocate memory for encoded data
	kif_rle_t *Encoded = (kif_rle_t *)malloc(Header->Width * Header->Height * sizeof(kif_rle_t));

	// Return if malloc failed
    if(Encoded == NULL){
        return NULL;
    }

    int EncodedIndex = 0; // Index for encoded data buffer

    // Run-length encode pixels
    int num_rle_entries = 0; // Number of run length encoded pixels

	int DataLen = Header->Width * Header->Height;
	
	uint32_t *px_data = (uint32_t *)Data;

	for(int i = 0; i < DataLen; i++){
		kif_rgba_t Color;
		kif_rle_t px_rle;

		Color.v = px_data[i];
		px_rle.pID = _in_palette(Color, Palette, &NumberOfColors);
		int rl = 0; // Move run length counter inside the loop

		// TODO: Looks like this RLE encoding caps the palette at 255 colors, all colors are in the palette, but only indexes 0-255 used.
		for(int Count = 0; Count < 255; Count++){
			if(Color.v == px_data[i]){
				rl++;
				i++;
			}else{
				break;
			}
		}
		
		i--;

		px_rle.rle = rl;
		Encoded[num_rle_entries] = px_rle;
		num_rle_entries++;

		rl = 0; // Reset run length counter to 0

		// Update encoded data index
		EncodedIndex++;
	}
	
	Header->Magic =  0x6B696631;	// 'kif1'
	Header->BPP = 4;
	Header->Compressed = 0;

    // Write number of run length encoded pixels to header
    Header->RLEEntries = num_rle_entries;

    // Calculate total size of the output buffer
    int TotalSize = sizeof(KIFHeader) + (sizeof(kif_rgba_t) * NumberOfColors) + (EncodedIndex * sizeof(kif_rle_t));

    // Allocate memory for the final output buffer
    unsigned char *OutputBuffer = (unsigned char *)malloc(TotalSize);

    // Copy header to output buffer
    memcpy(OutputBuffer, Header, sizeof(KIFHeader));

    // Copy palette to output buffer
    memcpy(OutputBuffer + sizeof(KIFHeader), Palette, (sizeof(kif_rgba_t) * NumberOfColors));	

    // Copy encoded data to output buffer
    memcpy(OutputBuffer +  sizeof(KIFHeader) + (sizeof(kif_rgba_t) * NumberOfColors), Encoded, (EncodedIndex * sizeof(kif_rle_t)));

    // Update output length
	*OutputLength = TotalSize;

	// Free allocated memory
	free(Encoded);

    // Return pointer to the output buffer
    return OutputBuffer;
}

/* --- Internal functions --- */

/**
 * Find and return the index of a color in the palette.
 */
static int _in_palette(kif_rgba_t Color, kif_rgba_t *Palette, int *NumberOfColors){
	for(int Index = 0; Index < *NumberOfColors; Index++){
		if(Palette[Index].v == Color.v){
			return Index;
		}
	}
	return -1;	// For some odd reason, we could not find the color in the palette.
}

/**
 * Generate a palette from a raw image. Array of RGBa values.
 */
static void _generate_palette(const void *Data, KIFHeader *Header, kif_rgba_t *Palette, int *NumberOfColors){
	kif_rgba_t *PaletteData = (kif_rgba_t *)Data;

    // Initialize palette with transparent black as the first entry
    Palette[0].v = 0x00000000;
    (*NumberOfColors)++;

	int PaletteDataLength = Header->Width * Header->Height;
	//int palette_index;	//	TODO: Looks like this can be deleted.
	int Pos;

	for(Pos = 0; Pos < PaletteDataLength; Pos++){
		int i;

        for(i = 0; i < *NumberOfColors; i++){
			if(Palette[i].v == PaletteData[Pos].v){
                //palette_index = i;
                break;
            }
        }

        // If color is not in the palette, add it
        if(i == *NumberOfColors){
            if(*NumberOfColors < 65536){ // Check if palette is full
                Palette[*NumberOfColors].v = PaletteData[Pos].v;
               // palette_index = *NumberOfColors;
                (*NumberOfColors)++;
            }/*else{ // If palette is full, use index 0 (reserved for palette overflow)
                palette_index = 0;
            }*/
        }
	}
}

// Function to read a 16-bit little-endian value from a buffer
static unsigned short _read16bit(const unsigned char *buffer) {
    return (buffer[1] << 8) | buffer[0];
}

// Function to read a 32-bit little-endian value from a buffer
static unsigned int _read32bit(const unsigned char *buffer) {
    return (buffer[3] << 24) | (buffer[2] << 16) | (buffer[1] << 8) | buffer[0];
}
