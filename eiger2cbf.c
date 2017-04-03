/*
EIGER HDF5 to CBF converter
 Written by Takanori Nakane
 (APPLY_FLATFIELD_CORRECTION by Keitaro Yamashita)

To build:

Linux

gcc -std=c99 -o eiger2cbf -g \
 -I$HOME/prog/dials/modules/cbflib/include \
 -L$HOME/prog/dials/build/lib -Ilz4 \
 eiger2cbf.c \
 lz4/lz4.c lz4/h5zlz4.c \
 bitshuffle/bshuf_h5filter.c \
 bitshuffle/bshuf_h5plugin.c \
 bitshuffle/bitshuffle.c \
 /usr/lib/x86_64-linux-gnu/libhdf5_hl.a \
 /usr/lib/x86_64-linux-gnu/libhdf5.a \
 -lcbf -lm -lpthread -lz -ldl

Mac OS X

gcc -std=c99 -o eiger2cbf -g \
 -ICBFlib-0.9.5.2/include -Ilz4 \
 eiger2cbf.c \
 lz4/lz4.c lz4/h5zlz4.c \
 bitshuffle/bshuf_h5filter.c \
 bitshuffle/bshuf_h5plugin.c \
 bitshuffle/bitshuffle.c \
 $HOME/local/lib/libhdf5_hl.a \
 $HOME/local/lib/libhdf5.a \
 CBFlib-0.9.5.2/lib/libcbf.a \
 -lm -lpthread -lz -ldl

*/

#include "stdio.h"
#include "stdlib.h"

#include "cbf.h"
#include "cbf_simple.h"
#include "hdf5.h"
#include "hdf5_hl.h"

// Comment out this line to skip flatfield correction.
#define APPLY_FLATFIELD_CORRECTION

extern const H5Z_class2_t H5Z_LZ4;
extern const H5Z_class2_t bshuf_H5Filter;
void register_filters() {
  H5Zregister(&H5Z_LZ4);
  H5Zregister(&bshuf_H5Filter);
}

int main(int argc, char **argv) {
  cbf_handle cbf;
  char header[4096] = {};
  int xpixels = -1, ypixels = -1, beamx = -1, beamy = -1, nimages = -1, depth = -1, countrate_cutoff = -1;
  int from = -1, to = -1;
  int ret;
  double pixelsize = -1, wavelength = -1, distance = -1, count_time = -1, frame_time = -1, osc_width = -1, osc_start = -9999, thickness = -1;
  char detector_sn[256] = {}, description[256] = {}, version[256] = {};

  hid_t hdf;

  fprintf(stderr, "EIGER HDF5 to CBF converter (version 160530)\n");
  fprintf(stderr, " written by Takanori Nakane\n");
  fprintf(stderr, " see https://github.com/biochem-fan/eiger2cbf for details.\n\n");

  if (argc <= 1 || argc >= 5) { 
    printf("Usage:\n");
    printf("  %s filename.h5           -- get number of frames\n", argv[0]);
    printf("  %s filename.h5 N out.cbf -- write N-th frame to out.cbf\n", argv[0]);
    printf("  %s filename.h5 N         -- write N-th frame to STDOUT\n", argv[0]);
    printf("  %s filename.h5 N:M   out -- write N to M-th frames to outNNNNNN.cbf\n", argv[0]);
    printf("  N starts from 1. The file should be \"master\" h5.\n");
    return -1;
  }

  register_filters();

  hdf = H5Fopen(argv[1], H5F_ACC_RDONLY, H5P_DEFAULT);
  if (hdf < 0) {
    fprintf(stderr, "Failed to open file %s\n", argv[1]);
    return -1;
  }

  H5LTread_dataset_int(hdf, "/entry/instrument/detector/detectorSpecific/nimages", &nimages);
  if (argc == 2) {
    printf("%d\n", nimages);
    H5Fclose(hdf);
    return 0;
  }
  
  ret = sscanf(argv[2], "%d:%d", &from, &to);
  if (ret == 0) {
    fprintf(stderr, "Failed to parse output frame number(s).");
    return -1;\
  } else if (ret == 1) {
    to = from;
  }
  if (to != from && argc < 4) {
    fprintf(stderr, "You cannot output multiple images into STDOUT.");
    return -1;
  }
  fprintf(stderr, "Going to convert frame %d to %d.\n", from, to);  
  
  H5Eset_auto(0, NULL, NULL); // Comment out this line for debugging.

  fprintf(stderr, "Metadata in HDF5:\n");
  H5LTread_dataset_string(hdf, "/entry/instrument/detector/description", description);
  fprintf(stderr, " /entry/instrument/detector/description = %s\n", description);
  H5LTread_dataset_string(hdf, "/entry/instrument/detector/detector_number", detector_sn);
  fprintf(stderr, " /entry/instrument/detector/detector_number = %s\n", detector_sn);
  H5LTread_dataset_string(hdf, "/entry/instrument/detector/detectorSpecific/software_version", version);
  fprintf(stderr, " /entry/instrument/detector/detectorSpecific/software_version = %s\n", version);
  H5LTread_dataset_int(hdf, "/entry/instrument/detector/bit_depth_image", &depth);
  if (depth > 0) {
    fprintf(stderr, " /entry/instrument/detector/bit_depth_image = %d\n", depth);
  } else {
    fprintf(stderr, " WARNING: /entry/instrument/detector/bit_depth_image is not avaialble. We assume 16 bit.\n");
    depth = 16;
  }
  unsigned int error_val = (unsigned int)(((unsigned long long)1 << depth) - 1);

  // Saturation value
  // Firmware >= 1.5
  H5LTread_dataset_int(hdf, "/entry/instrument/detector/detectorSpecific/saturation_value", &countrate_cutoff);
  if (countrate_cutoff > 0) {
    fprintf(stderr, " /entry/instrument/detector/detectorSpecific/saturation_value = %d\n", countrate_cutoff);
  } else {
    // Firmware >= 1.4
    fprintf(stderr, "  /entry/instrument/detector/detectorSpecific/saturation_value not present. Trying another place.\n");
    H5LTread_dataset_int(hdf, "/entry/instrument/detector/detectorSpecific/countrate_correction_count_cutoff", &countrate_cutoff);
    if (countrate_cutoff > 0) {
      fprintf(stderr, " /entry/instrument/detector/detectorSpecific/countrate_correction_count_cutoff = %d\n", countrate_cutoff);
      countrate_cutoff++;
    } else {
      fprintf(stderr, "  /entry/instrument/detector/detectorSpecific/countrate_correction_count_cutoff not present. Trying another place.\n");
      // < 1.4
      H5LTread_dataset_int(hdf, "/entry/instrument/detector/detectorSpecific/detectorModule_000/countrate_correction_count_cutoff", &countrate_cutoff);
      if (countrate_cutoff > 0) {
        fprintf(stderr, " /entry/instrument/detector/detectorSpecific/detectorModule_000/countrate_correction_count_cutoff = %d\n", countrate_cutoff);
	fprintf(stderr, "  WARNING: The use of this field is not recommended now.\n");
	fprintf(stderr, "  You might want to change the OVERLOAD setting in your subsequent processing.\n");
        countrate_cutoff++;
      } else {
        fprintf(stderr, "  /entry/instrument/detector/detectorSpecific/detectorModule_000/countrate_correction_count_cutoff not present.\n");
        countrate_cutoff = error_val - 1;
        fprintf(stderr, "  As a last resort, we will put an arbitrary large number (%d) in the header.\n", countrate_cutoff);
        fprintf(stderr, "  You might want to change the OVERLOAD setting in your subsequent processing.\n");
      }
    }
  }

  H5LTread_dataset_double(hdf, "/entry/instrument/detector/sensor_thickness", &thickness); // in m 
  if (thickness > 0) {
    fprintf(stderr, " /entry/instrument/detector/sensor_thickness = %f (um)\n", thickness * 1E6);
  } else {
    thickness = 450E-6;
    fprintf(stderr, " /entry/instrument/detector/sensor_thickness is not avaialble. We assume it is %f um\n", thickness * 1E6);
  }
  H5LTread_dataset_int(hdf, "/entry/instrument/detector/detectorSpecific/x_pixels_in_detector", &xpixels);
  H5LTread_dataset_int(hdf, "/entry/instrument/detector/detectorSpecific/y_pixels_in_detector", &ypixels);
  fprintf(stderr, " /entry/instrument/detector/detectorSpecific/{x,y}_pixels_in_detector = (%d, %d) (px)\n",
	  xpixels, ypixels);
  H5LTread_dataset_int(hdf, "/entry/instrument/detector/beam_center_x", &beamx);
  H5LTread_dataset_int(hdf, "/entry/instrument/detector/beam_center_y", &beamy);
  fprintf(stderr, " /entry/instrument/detector/beam_center_{x,y} = (%d, %d) (px)\n", beamx, beamy);
  H5LTread_dataset_double(hdf, "/entry/instrument/detector/count_time", &count_time); // in m
  fprintf(stderr, " /entry/instrument/detector/count_time = %f (sec)\n", count_time);
  H5LTread_dataset_double(hdf, "/entry/instrument/detector/frame_time", &frame_time); // in 
  fprintf(stderr, " /entry/instrument/detector/frame_time = %f (sec)\n", frame_time);
  H5LTread_dataset_double(hdf, "/entry/instrument/detector/x_pixel_size", &pixelsize); // in 
  fprintf(stderr, " /entry/instrument/detector/x_pixel_size = %f (m)\n", pixelsize);

  // Detector distance

  H5LTread_dataset_double(hdf, "/entry/instrument/detector/distance", &distance); // Firmware >= 1.7
  if (distance > 0) {
    fprintf(stderr, " /entry/instrument/detector/distance = %f (m)\n", distance);
  } else {
    fprintf(stderr, "  /entry/instrument/detector/distance not present. Trying another place.\n");

    H5LTread_dataset_double(hdf, "/entry/instrument/detector/detector_distance", &distance); // Firmware< 1.7
    if (distance > 0) {
      fprintf(stderr, " /entry/instrument/detector/detector_distance = %f (m)\n", distance);
    } else {
      fprintf(stderr, "  /entry/instrument/detector/detector_distance not present.\n");
      fprintf(stderr, " WARNING: detector distance was not defined! \"Detector distance\" field in the output is set to -1.\n");
    }
  }

  // Wavelength
  H5LTread_dataset_double(hdf, "/entry/sample/beam/incident_wavelength", &wavelength); // Firmware >= 1.7
  if (wavelength > 0) {
    fprintf(stderr, " /entry/sample/beam/incident_wavelength = %f (A)\n", wavelength);
  } else {
    fprintf(stderr, "  /entry/sample/beam/incident_wavelength not present. Trying another place.\n");

    H5LTread_dataset_double(hdf, "/entry/instrument/beam/wavelength", &wavelength);
    if (wavelength > 0) {
      fprintf(stderr, " /entry/instrument/beam/wavelength = %f (A)\n", wavelength);
    } else {
      fprintf(stderr, "  /entry/instrument/beam/wavelength not present. Trying another place.\n");

      H5LTread_dataset_double(hdf, "/entry/instrument/monochromator/wavelength", &wavelength);
      if (wavelength > 0) {
	fprintf(stderr, " /entry/instrument/monochromator/wavelength = %f (A)\n", wavelength);
      } else {
	fprintf(stderr, "  /entry/instrument/monochromator/wavelength not present. Trying another place.\n");

	H5LTread_dataset_double(hdf, "/entry/instrument/beam/incident_wavelength", &wavelength); // Firmware 1.6
	if (wavelength > 0) {
	  fprintf(stderr, " /entry/instrument/beam/incident_wavelength = %f (A)\n", wavelength);
	} else {
	  fprintf(stderr, "  /entry/instrument/beam/incident_wavelength not present.\n");
	}
      }
    }
  }
  if (wavelength < 0) {
    fprintf(stderr, " WARNING: wavelength was not defined! \"Wavelength\" field in the output is set to -1.\n");
  }

  H5LTread_dataset_double(hdf, "/entry/sample/goniometer/omega_range_average", &osc_width);
  if (osc_width > 0) {
    fprintf(stderr, " /entry/sample/goniometer/omega_range_average = %f (deg)\n", osc_width);
  } else {
    fprintf(stderr, " WARNING: oscillation width was not defined. \"Start_angle\" field in the output is set to 0!\n");
    osc_width = 0;
  }
  unsigned int *buf = (unsigned int*)malloc(sizeof(unsigned int) * xpixels * ypixels);
  signed int *buf_signed = (signed int*)malloc(sizeof(signed int) * xpixels * ypixels);
  signed int *pixel_mask = (signed int*)malloc(sizeof(signed int) * xpixels * ypixels);
#ifdef APPLY_FLATFIELD_CORRECTION
  float *flatfield = (float*)malloc(sizeof(float) * xpixels * ypixels);
  int flatfield_correction_applied;
  if (flatfield == NULL) {
    fprintf(stderr, "Failed to allocate flatfield.\n");
    return -1;
  }
#endif
  if (buf == NULL || buf_signed == NULL || pixel_mask == NULL) {
    fprintf(stderr, "Failed to allocate image buffer.\n");
    return -1;
  }

  // TODO: Is it always in omega?
  int bufsize = (nimages < 100000) ? 100000 : nimages;
  double *angles = (double*)malloc(bufsize * sizeof(double));
  // I don't know why but nimages can be too small ...
  if (angles == NULL) {
    fprintf(stderr, "failed to allocate buffer for omega.\n");
    return -1;
  }
  angles[0] = -9999;
  H5LTread_dataset_double(hdf, "/entry/sample/goniometer/omega", angles);
  fprintf(stderr, "\n");

  hid_t entry, group;
  entry = H5Gopen2(hdf, "/entry", H5P_DEFAULT);
  if (entry < 0) {
    fprintf(stderr, "/entry does not exist!\n");
    return -1;
  }

  pixel_mask[0] = -9999;
  H5LTread_dataset_int(hdf, "/entry/instrument/detector/detectorSpecific/pixel_mask", pixel_mask);
  if (pixel_mask[0] == -9999) {
    fprintf(stderr, "WARNING: failed to read the pixel mask from /entry/instrument/detector/detectorSpecific/pixel_mask.\n");
    fprintf(stderr, " Thus, we mask pixels whose intensity is %u (= (2 ^ bit_depth_image) - 1) by converting them to -1. \n", error_val);
    fprintf(stderr, " However, this might mask overloaded (saturated) pixels as well.\n");
  }

#ifdef APPLY_FLATFIELD_CORRECTION
  H5LTread_dataset_int(hdf, "/entry/instrument/detector/flatfield_correction_applied", &flatfield_correction_applied);
  if (flatfield_correction_applied == 1)
    fprintf(stderr, "/entry/instrument/detector/flatfield_correction_applied is 1! skipping flatfield correction.\n");
  else if (flatfield_correction_applied == 0) {
    flatfield[0] = 100.;
    H5LTread_dataset_float(hdf, "/entry/instrument/detector/detectorSpecific/flatfield", flatfield);
    if (flatfield[0] == 100.) {
      fprintf(stderr, "WARNING: failed to read the flatfield from /entry/instrument/detector/detectorSpecific/flatfield.\n");
      return -1;
    }
    fprintf(stderr, "/entry/instrument/detector/flatfield_correction_applied is 0! applying flatfield correction.\n");
  }
  else {
    fprintf(stderr, "/entry/instrument/detector/flatfield_correction_applied is neither 0 or 1!");
    return -1;
  }
#endif

  // Check if /entry/data present
  group = H5Gopen2(entry, "data", H5P_DEFAULT);  
  if (group < 0) {
    group = entry; // leak!
  }

  int block_start = 1;
  if (H5LTfind_dataset(group, "data_000000")) {
    fprintf(stderr, "This dataset starts from data_000000.\n");
    block_start = 0;
  } else {
    fprintf(stderr, "This dataset starts from data_000001.\n");
  }

  char data_name[20] = {};
  hid_t data, dataspace;
  int number_per_block = 0;
  
  // Open the first data block to get the number of frames in a block
  snprintf(data_name, 20, "data_%06d", block_start); 
  data = H5Dopen2(group, data_name, H5P_DEFAULT);
  dataspace = H5Dget_space(data);
  if (data < 0) {
    fprintf(stderr, "failed to open /entry/%s\n", data_name);
    return -1;
  }
  if (H5Sget_simple_extent_ndims(dataspace) != 3) {
    fprintf(stderr, "Dimension of /entry/%s is not 3!\n", data_name);
    return -1;    
  }
  hsize_t dims[3];
  H5Sget_simple_extent_dims(dataspace, dims, NULL);
  number_per_block = dims[0];
  fprintf(stderr, "The number of images per data block is %d.\n", number_per_block);

  H5Sclose(dataspace);
  H5Dclose(data);

  fprintf(stderr, "\nFile analysis completed.\n\n");
  int frame;
  for (frame = from; frame <= to; frame++) {
    fprintf(stderr, "Converting frame %d (%d / %d)\n", frame, frame - from + 1, to - from + 1);
    if (angles[0] != -9999) {
      osc_start = angles[frame - 1];
      fprintf(stderr, " /entry/sample/goniometer/omega[%d] = %.3f (1-indexed)\n", frame, osc_start);
    } else {
      fprintf(stderr, " oscillation start not defined. \"Start_angle\" field in the output is set to 0!\n");
      osc_start = osc_width * frame; // old firmware
    }

    if (frame > nimages) {
      fprintf(stderr, "WARNING: invalid frame number specified. %d is bigger than nimages (%d)\n", frame, nimages);
      // Due to a firmware bug, nimages can be smaller than the actual value.
      // So we don't exit here
    }

    char header_format[] = 
      "\n"
      "# Detector: %s, S/N %s\n"
      "# Pixel_size %de-6 m x %de-6 m\n"
      "# Silicon sensor, thickness %.6f m\n"
      "# Exposure_time %f s\n"
      "# Exposure_period %f s\n"
      "# Count_cutoff %d counts\n"
      "# Wavelength %f A\n"
      "# Detector_distance %f m\n"
      "# Beam_xy (%d, %d) pixels\n"
      "# Start_angle %f deg.\n"
      "# Angle_increment %f deg.\n";

    char header_content[4096] = {};
    snprintf(header_content, 4096, header_format,
	     description, detector_sn,
	     thickness,
	     (int)(pixelsize * 1E6), (int)(pixelsize * 1E6),
	     count_time, frame_time, countrate_cutoff, wavelength, distance,
	     beamx, beamy, osc_start, osc_width);


    // Now open the required data

    int block_number = block_start + (frame - 1) / number_per_block;
    int frame_in_block = (frame - 1) % number_per_block;
//    fprintf(stderr, " frame %d is in data_%06d frame %d (1-indexed).\n", 
//            frame, block_number, frame_in_block + 1);
    
    snprintf(data_name, 20, "data_%06d", block_number); 
    data = H5Dopen2(group, data_name, H5P_DEFAULT);
    dataspace = H5Dget_space(data);
    if (data < 0) {
      fprintf(stderr, "failed to open /entry/%s\n", data_name);
      return -1;
    }
    if (H5Sget_simple_extent_ndims(dataspace) != 3) {
      fprintf(stderr, "Dimension of /entry/%s is not 3!\n", data_name);
      return -1;    
    }

    // Get the frame
    H5Sget_simple_extent_dims(dataspace, dims, NULL);
    hsize_t offset_in[3] = {frame_in_block, 0, 0};
    hsize_t offset_out[3] = {0, 0, 0};
    hsize_t count[3] = {1, ypixels, xpixels};
    hid_t memspace = H5Screate_simple(3, dims, NULL);   
    if (memspace < 0) {
      fprintf(stderr, "failed to create memspace\n");
      return -1;
    }

    ret = H5Sselect_hyperslab(dataspace, H5S_SELECT_SET, offset_in, NULL, 
			      count, NULL);
    if (ret < 0) {
      fprintf(stderr, "select_hyperslab for file failed\n");
      return -1;
    }
    ret = H5Sselect_hyperslab(memspace, H5S_SELECT_SET, offset_out, NULL, 
			      count, NULL);
    if (ret < 0) {
      fprintf(stderr, "select_hyperslab for memory failed\n");
      return -1;
    }
    ret = H5Dread(data, H5T_NATIVE_UINT, memspace, dataspace, H5P_DEFAULT, buf);
    if (ret < 0) {
      fprintf(stderr, "H5Dread for image failed. Wrong frame number?\n");
      return -1;
    }
    H5Sclose(dataspace);
    H5Sclose(memspace);
    H5Dclose(data);

    /////////////////////////////////////////////////////////////////
    // Reading done. Here output starts...

    FILE *fh = stdout;

    if (argc > 3) {
      if (from == to) {
	fh = fopen(argv[3], "wb");
      } else {
	char filename[4096];
	snprintf(filename, 4096, "%s%06d.cbf", argv[3], frame);
	fh = fopen(filename, "wb");
      }
    }

    // create a CBF
    cbf_make_handle(&cbf);
    cbf_new_datablock(cbf, "image_1");

    // put a miniCBF header
    cbf_new_category(cbf, "array_data");
    cbf_new_column(cbf, "header_convention");
    cbf_set_value(cbf, "SLS_1.0");
    cbf_new_column(cbf, "header_contents");
    cbf_set_value(cbf, header_content);

    // put the image
    cbf_new_category(cbf, "array_data");
    cbf_new_column(cbf, "data");
    int i;
    for (i = 0; i < xpixels * ypixels; i++) {
      if ((pixel_mask[0] != -9999 && pixel_mask[i] == 1) || // the pixel mask is available
	  (pixel_mask[0] == -9999 && buf[i] == error_val)) { // not available
	buf_signed[i] = -1;
      } else if (pixel_mask[0] != -9999 && pixel_mask[i] > 1) { // the pixel mask is 2, 4, 8, 16
	buf_signed[i] = -2;
      } else {
#ifdef APPLY_FLATFIELD_CORRECTION
	buf_signed[i] = flatfield_correction_applied == 0 ? buf[i] * flatfield[i] + 0.5 : buf[i];
#else
	buf_signed[i] = buf[i];
#endif
      }
    }
    cbf_set_integerarray_wdims_fs(cbf,
				  CBF_BYTE_OFFSET,
				  1, // binary id
				  buf_signed,
				  sizeof(int),
				  1, // signed?
				  xpixels * ypixels,
				  "little_endian",
				  xpixels,
				  ypixels,
				  0,
				  0); //padding

    cbf_write_file(cbf, fh, 1, CBF, MSG_DIGEST | MIME_HEADERS | PAD_4K, 0);
    // no need to fclose() here as the 3rd argument "readable" is 1
    cbf_free_handle(cbf);
  }

  H5Gclose(group);
  H5Fclose(hdf);

  free(buf);
  free(buf_signed);
  free(angles);

  fprintf(stderr, "\nAll done!\n");

  return 0;
}
