/*
 * Reading the ST-NICCC megademo data stored in
 * the SPI flash and streaming it to polygons,
 * rendered as ANSI character sequences through
 * the UART.
 * 
 * The polygon stream is a 640K file, that needs
 * to be stored in the SPI flash, using:
 * ICEStick: iceprog -o 1M EXAMPLES/DATA/scene1.dat
 * ULX3S:    cp EXAMPLES/DATA/scene1.dat scene1.img
 *           ujprog -j flash -f 1048576 scene1.img
 *   (using latest version of ujprog compiled from https://github.com/kost/fujprog)
 *
 * More details and links in EXAMPLES/DATA/notes.txt
 */

#include <stdint.h>
#include <stdio.h>

#ifdef __linux__
#include <stdlib.h>
#include <unistd.h>
#endif

#define MIN(x,y) ((x) < (y) ? (x) : (y))
#define MAX(x,y) ((x) > (y) ? (x) : (y))

/**********************************************************************************/
/* Graphics routines                                                              */
/**********************************************************************************/

// SHRINK=1: 256x128 (you will need to set a tiny font in the tty to see smthg)
// SHRINK=2: 128x64
#define SHRINK 2

void GL_clear() {
    // We could clear the screen here, but it will flicker too much !
}

void GL_fill_poly(int nb_pts, int* points, int color) {
    char x_left[128];
    char x_right[128];

    /* Determine clockwise, miny, maxy */
    int clockwise = 0;
    int minx =  256;
    int maxx = -256;
    int miny =  256;
    int maxy = -256;

    /* 
     * Set background color using 6x6x6 colorcube codes
     * see https://stackoverflow.com/questions/4842424/list-of-ansi-color-escape-sequences
     */

    printf("\033[48;5;%dm",color);
    
    for(int i1=0; i1<nb_pts; ++i1) {
	int i2=(i1==nb_pts-1) ? 0 : i1+1;
	int i3=(i2==nb_pts-1) ? 0 : i2+1;
	int x1 = points[2*i1];
	int y1 = points[2*i1+1];
	int dx1 = points[2*i2]   - x1;
	int dy1 = points[2*i2+1] - y1;
	int dx2 = points[2*i3]   - x1;
	int dy2 = points[2*i3+1] - y1;
	clockwise += dx1 * dy2 - dx2 * dy1;
	minx = MIN(minx,x1);
	maxx = MAX(maxx,x1);
	miny = MIN(miny,y1);
	maxy = MAX(maxy,y1);
    }

    /* Determine x_left and x_right for each scaline */
    for(int i1=0; i1<nb_pts; ++i1) {
	int i2=(i1==nb_pts-1) ? 0 : i1+1;

	int x1 = points[2*i1];
	int y1 = points[2*i1+1];
	int x2 = points[2*i2];
	int y2 = points[2*i2+1];

	char* x_buffer = ((clockwise > 0) ^ (y2 > y1)) ? x_left : x_right;
	int dx = x2 - x1;
	int sx = 1;
	int dy = y2 - y1;
	int sy = 1;
	int x = x1;
	int y = y1;
	int ex;
	
	if(dx < 0) {
	    sx = -1;
	    dx = -dx;
	}
	
	if(dy < 0) {
	    sy = -1;
	    dy = -dy;
	}

	if(y1 == y2) {
	  x_left[y1]  = MIN(x1,x2);
	  x_right[y1] = MAX(x1,x2);
	  continue;
	}

	ex = (dx << 1) - dy;

	for(int u=0; u <= dy; ++u) {
    	    x_buffer[y] = x; 
	    y += sy;
	    while(ex >= 0) {
		x += sx;
		ex -= dy << 1;
	    }
	    ex += dx << 1;
	}
    }

    for(int y = miny; y <= maxy; ++y) {
	int x1 = x_left[y];
	int x2 = x_right[y];
	// Goto_XY(x1,y)
	printf("\033[%d;%dH",y,x1);
	for(int x=x1; x<=x2; ++x) {
	    putchar(' ');
	}
    }
}


/**********************************************************************************/

/*
 * Starting address of data stream stored in the 
 * SPI.
 * I put the data stream starting from 1M offset,
 * just to make sure it does not collide with
 * FPGA wiring configuration ! (but FPGA configuration
 * only takes a few tenth of kilobytes I think).
 * Using the IO interface, it is using the physical address
 *  (starting at 1M). Using the mapped memory interface,
 *  SPI_FLASH_BASE is mapped to 1M.
 */
uint32_t spi_addr = 0;

/*
 * Word address and cached word used in mapped mode
 */
uint32_t spi_word_addr = 0;
union {
  uint32_t spi_word;
  uint8_t spi_bytes[4];
} spi_u;

#define ADDR_OFFSET 1024*1024

/*
 * Restarts reading from the beginning of the stream.
 */
void spi_reset() {
  spi_addr = ADDR_OFFSET;
  spi_word_addr = (uint32_t)(-1);
}


#ifdef __linux__

FILE* f = NULL;

/**
 * Reads one byte of data from the file (emulates read_spi_byte() when running on desktop)
 */
uint8_t next_spi_byte() {
   uint8_t result;
   if(f == NULL) {
      f = fopen("../../../FIRMWARE/EXAMPLES/DATA/scene1.dat","rb");
      if(f == NULL) {
	 printf("Could not open data file\n");
	 exit(-1);
      }
   }
   if(spi_word_addr != spi_addr >> 2) {
      spi_word_addr = spi_addr >> 2;
      fseek(f, spi_word_addr*4-ADDR_OFFSET, SEEK_SET);
      fread(&(spi_u.spi_word), 4, 1, f);
   }
   result = spi_u.spi_bytes[spi_addr&3];
   ++spi_addr;
   return (uint8_t)(result);
}

#else


# define SPI_FLASH_BASE ((uint32_t*)(1 << 23))

/**
 * Reads one byte from the SPI flash, using the mapped SPI flash interface.
 */
uint8_t next_spi_byte() {
   uint8_t result;
   if(spi_word_addr != spi_addr >> 2) {
      spi_word_addr = spi_addr >> 2;
      spi_u.spi_word = SPI_FLASH_BASE[spi_word_addr];
   }
   result = spi_u.spi_bytes[spi_addr&3];
   ++spi_addr;
   return (uint8_t)(result);
}

#endif

uint16_t next_spi_word() {
   /* In the ST-NICCC file,  
    * words are stored in big endian format.
    * (see DATA/scene_description.txt).
    */
   uint16_t hi = (uint16_t)next_spi_byte();    
   uint16_t lo = (uint16_t)next_spi_byte();
   return (hi << 8) | lo;
}

/* 
 * The colormap, encoded in such a way that it
 * can be directly sent as ANSI color codes.
 */
int cmap[16];

/*
 * Current frame's vertices coordinates (if frame is indexed),
 *  mapped to OLED display dimensions (divide by 2 from file).
 */
uint8_t  X[255];
uint8_t  Y[255];

/*
 * Current polygon vertices, as expected
 * by GL_fill_poly():
 * xi = poly[2*i], yi = poly[2*i+1]
 */
int      poly[30];

/*
 * Masks for frame flags.
 */
#define CLEAR_BIT   1
#define PALETTE_BIT 2
#define INDEXED_BIT 4

/*
 * Reads a frame's polygonal description from
 * SPI flash and rasterizes the polygons using
 * FemtoGL.
 * returns 0 if last frame.
 *   See DATA/scene_description.txt for the 
 * ST-NICCC file format.
 *   See DATA/test_ST_NICCC.c for an example
 * program.
 */
int read_frame() {
    uint8_t frame_flags = next_spi_byte();

    // Update palette data.
    if(frame_flags & PALETTE_BIT) {
	uint16_t colors = next_spi_word();
	for(int b=15; b>=0; --b) {
	    if(colors & (1 << b)) {
		int rgb = next_spi_word();
	       
		// Get the three 3-bits per component R,G,B
	        int b3 = (rgb & 0x007);
		int g3 = (rgb & 0x070) >> 4;
		int r3 = (rgb & 0x700) >> 8;

		// Re-encode them as ANSI 8-bits color
		if(b3 > 5) b3 = 5;
		if(g3 > 5) g3 = 5;
		if(r3 > 5) r3 = 5;		

		cmap[15-b] = 16 + b3 + 6*(g3 + 6*r3);
	    }
	}
    }

    if(frame_flags & CLEAR_BIT) {
	GL_clear(); 
    }

    // Update vertices
    if(frame_flags & INDEXED_BIT) {
	uint8_t nb_vertices = next_spi_byte();
	for(int v=0; v<nb_vertices; ++v) {
	    X[v] = (next_spi_byte() >> (SHRINK-1));
	    Y[v] = (next_spi_byte() >> SHRINK);
	}
    }

    // Draw frame's polygons
    for(;;) {
	uint8_t poly_desc = next_spi_byte();

	// Special polygon codes (end of frame,
	// seek next block, end of stream)
	
	if(poly_desc == 0xff) {
	    break; // end of frame
	}
	if(poly_desc == 0xfe) {
	    // Go to next 64kb block
	    spi_addr -= ADDR_OFFSET;
	    spi_addr &= ~65535;
	    spi_addr +=  65536;
	    spi_addr += ADDR_OFFSET;
	    return 1; 
	}
	if(poly_desc == 0xfd) {
	    return 0; // end of stream
	}
	
	uint8_t nvrtx = poly_desc & 15;
	uint8_t poly_col = poly_desc >> 4;
	for(int i=0; i<nvrtx; ++i) {
	    if(frame_flags & INDEXED_BIT) {
		uint8_t index = next_spi_byte();
		poly[2*i]   = X[index];
		poly[2*i+1] = Y[index];
	    } else {
		poly[2*i]   = (next_spi_byte() >> (SHRINK-1));
		poly[2*i+1] = (next_spi_byte() >> SHRINK);
	    }
	}
	GL_fill_poly(nvrtx,poly,cmap[poly_col]);
    }
    return 1; 
}


int main() {
    GL_clear();
    for(;;) {
        spi_reset();
	while(read_frame()) {
#ifdef __linux__       
        usleep(20000);
#endif       
	}
    }
}
