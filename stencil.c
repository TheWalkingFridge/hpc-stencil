#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <mpi.h>

// Define output file name
#define OUTPUT_FILE "stencil.pgm"

#define MASTER 0

void stencil(const int nx, const int ny, const int width,
             float* image, float* tmp_image);
void init_image(const int nx, const int ny, const int width, const int height,
                float* image, float* tmp_image);
void output_image(const char* file_name, const int nx, const int ny,
                  const int width, const int height, float* image);
double wtime(void);

int main(int argc, char* argv[])
{
  MPI_Init(&argc, &argv);

  // Check usage
  if (argc != 4) {
    fprintf(stderr, "Usage: %s nx ny niters\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  // Init MPI
  int nprocs, rank;
  MPI_Comm_size(MPI_COMM_WORLD, &nprocs);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  // Initiliase problem dimensions from command line arguments
  int nx = atoi(argv[1]);
  int ny = atoi(argv[2]);
  int niters = atoi(argv[3]);

  // we pad the outer edge of the image to avoid out of range address issues in
  // stencil
  int width = nx + 2;
  int height = ny + 2;

  // Calculating the height of the slices
  int sliceHeight = ny / nprocs + 2; // including padding
  if(rank == nprocs - 1) sliceHeight += ny % nprocs;
  int sliceSize = sliceHeight * width; // including padding

  float* image;
  float* temp_image;

  if(rank == MASTER) {
    // Allocate the image
    image = malloc(sizeof(float) * width * height);
    temp_image = malloc(sizeof(float) * width * height);

    // Set the input image
    init_image(nx, ny, width, height, image, temp_image);
  }

  float* image_slice = malloc(sizeof(float) * sliceSize);
  float* image_temp_slice = malloc(sizeof(float) * sliceSize);

  int below = rank == (nprocs - 1) ? MPI_PROC_NULL : rank + 1;
  int above = (rank == MASTER) ? MPI_PROC_NULL : rank - 1;
  MPI_Status status;

  // printf("Rank: %d, below: %d, above: %d\n", rank, below, above);   

  // Distribute the data
  int payloadSliceSize = (ny / nprocs) * width;
  MPI_Scatter(image + width, payloadSliceSize, MPI_FLOAT,
 	      image_slice + width, payloadSliceSize, MPI_FLOAT,
	      MASTER, MPI_COMM_WORLD);

 if(nprocs != 1) {  
  if(rank == nprocs - 1) {
    int diff = ny % nprocs;
    int imageOffset = sliceHeight - diff;

    MPI_Recv(
      image_slice + width,
      sliceSize - 1 * width,
      MPI_FLOAT,
      MASTER,
      0,
      MPI_COMM_WORLD,
      &status);
  }

  if(rank == MASTER) {
    int diff = ny % nprocs;
    int s = (ny / nprocs + ny % nprocs) * width;

    MPI_Send(
      image + (width * (height - 1)) - s,
      s + width,
      MPI_FLOAT,
      nprocs - 1,
      0,
      MPI_COMM_WORLD);
  }
 }
  // Call the stencil kernel
  double tic = wtime();
  for (int t = 0; t < niters; ++t) {
    MPI_Sendrecv(image_slice + width, width, MPI_FLOAT,
                above, 0,
                image_slice + width * (sliceHeight - 1), width, MPI_FLOAT,
                below, 0,
                MPI_COMM_WORLD, &status);    

    MPI_Sendrecv(image_slice + width * (sliceHeight - 2), width, MPI_FLOAT,
                below, 0,
                image_slice, width, MPI_FLOAT,
                above, 0,
                MPI_COMM_WORLD, &status); 
    stencil(sliceHeight - 2, nx, width, image_slice, image_temp_slice);

    MPI_Sendrecv(image_temp_slice + width, width, MPI_FLOAT,
                above, 0,
                image_temp_slice + width * (sliceHeight - 1), width, MPI_FLOAT,
                below, 0,
                MPI_COMM_WORLD, &status);    

    MPI_Sendrecv(image_temp_slice + width * (sliceHeight - 2), width, MPI_FLOAT,
                below, 0,
                image_temp_slice, width, MPI_FLOAT,
                above, 0,
                MPI_COMM_WORLD, &status); 

    stencil(sliceHeight - 2, nx, width, image_temp_slice, image_slice);
  }

  double toc = wtime();

  if(rank == MASTER ) {

 	 // Output
 	 printf("------------------------------------\n");
 	 printf(" runtime: %lf s\n", toc - tic);
 	 printf("------------------------------------\n");

  }

  MPI_Gather(image_slice + width, payloadSliceSize, MPI_FLOAT,
	     image + width, payloadSliceSize, MPI_FLOAT,
	     MASTER, MPI_COMM_WORLD);

  // printf("Gather complete!\n");
 if(nprocs != 1) {
  if(rank == nprocs - 1) {
    int diff = ny % nprocs;
    int imageOffset = sliceHeight - diff;

    MPI_Send(
      image_slice + width,
      sliceSize - 2 * width,
      MPI_FLOAT,
      MASTER,
      0,
      MPI_COMM_WORLD);
  }

  if(rank == MASTER) {
    int diff = ny % nprocs;
    int s = (ny / nprocs + ny % nprocs) * width;

    MPI_Recv(
      image + (width * (height - 1)) - s,
      s,
      MPI_FLOAT,
      nprocs - 1,
      0,
      MPI_COMM_WORLD,
      &status);
  }
 }
  if(rank == MASTER) {
    output_image(OUTPUT_FILE, nx, ny, width, height, image);
    free(image);
    free(temp_image);
  }

  MPI_Barrier(MPI_COMM_WORLD);

  free(image_slice);
  free(image_temp_slice);

  MPI_Finalize();
}

void stencil(const int nx, const int ny, const int width,
             float* restrict image, float* restrict tmp_image)
{
  for (int i = 1; i < nx + 1; ++i) {
    for (int j = 1; j < ny + 1; ++j) {
      tmp_image[j + i * width] = (image[j     + i       * width] * 6.0f
                                    + image[j     + (i - 1) * width]
                                    + image[j     + (i + 1) * width]
                                    + image[j - 1 + i       * width]
                                    + image[j + 1 + i       * width]) * 0.1f;
        }
  }
}

// Create the input image
void init_image(const int nx, const int ny, const int width, const int height,
                float* image, float* tmp_image)
{
  // Zero everything
  for (int j = 0; j < ny + 2; ++j) {
    for (int i = 0; i < nx + 2; ++i) {
      image[j + i * height] = 0.0;
      tmp_image[j + i * height] = 0.0;
    }
  }

  const int tile_size = 64;
  // checkerboard pattern
  for (int jb = 0; jb < ny; jb += tile_size) {
    for (int ib = 0; ib < nx; ib += tile_size) {
      if ((ib + jb) % (tile_size * 2)) {
        const int jlim = (jb + tile_size > ny) ? ny : jb + tile_size;
        const int ilim = (ib + tile_size > nx) ? nx : ib + tile_size;
        for (int j = jb + 1; j < jlim + 1; ++j) {
          for (int i = ib + 1; i < ilim + 1; ++i) {
            image[j + i * height] = 100.0;
          }
        }
      }
    }
  }
}

// Routine to output the image in Netpbm grayscale binary image format
void output_image(const char* file_name, const int nx, const int ny,
                  const int width, const int height, float* image)
{
  // Open output file
  FILE* fp = fopen(file_name, "w");
  if (!fp) {
    fprintf(stderr, "Error: Could not open %s\n", OUTPUT_FILE);
    exit(EXIT_FAILURE);
  }

  // Ouptut image header
  fprintf(fp, "P5 %d %d 255\n", nx, ny);

  // Calculate maximum value of image
  // This is used to rescale the values
  // to a range of 0-255 for output
  double maximum = 0.0;
  for (int j = 1; j < ny + 1; ++j) {
    for (int i = 1; i < nx + 1; ++i) {
      if (image[j + i * height] > maximum) maximum = image[j + i * height];
    }
  }

  // Output image, converting to numbers 0-255
  for (int i = 1; i < nx + 1; ++i) {
    for (int j = 1; j < ny + 1; ++j) {
      fputc((char)(255.0 * image[j + i * height] / maximum), fp);
    }
  }

  // Close the file
  fclose(fp);
}

// Get the current time in seconds since the Epoch
double wtime(void)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec + tv.tv_usec * 1e-6;
}
