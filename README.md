eiger2cbf: EIGER HDF5 to miniCBF converter
==========================================

eiger2cbf is a simple program that converts diffraction images from
EIGER in the HDF5 format to the miniCBF format. This program is intended
to be used with MOSFLM. You do *not* need this program to process images
with [DIALS](http://dials.diamond.ac.uk/); DIALS can process HDF5 images 
directly (DIALS 1.2.0, which comes with CCP4 7.0.13).

*NOTE:* As of late 2018, MOSFLM no longer needs conversion to CBF (see https://www.mrc-lmb.cam.ac.uk/harry/mosflm-hdf5/).
eiger2cbf does not support NeXus files from Diamond Light Source, while the above mentioned version of MOSFLM supports it.

Installation
------------

You can get static-linked binaries built by Harry Powell from his website.
See the bottom of the
[iMosflm download page](http://www.mrc-lmb.cam.ac.uk/harry/imosflm/ver721/downloads.html#Eiger2CBF).
Note that the binaries are not necessarily the latest version (160415).

To build yourself, you should edit Makefile and run `make`.

Running eiger2cbf without command line options shows a help.

```
$ eiger2cbf
EIGER HDF5 to CBF converter (build 160310)
 Written by Takanori Nakane

Usage:
  ./eiger2cbf filename.h5           -- get number of frames
  ./eiger2cbf filename.h5 N out.cbf -- write N-th frame to out.cbf
  ./eiger2cbf filename.h5 N         -- write N-th frame to STDOUT
  ./eiger2cbf filename.h5 N:M   out -- write N to M-th frames to outNNNNNN.cbf
  N starts from 1. The file should be "master" h5.
```

H5ToXds compatibility
---------------------

The command line option is almost the same as H5ToXds from
Dectris. Actually, you can use eiger2cbf instead of H5ToXds. This
might be useful on Mac OS X because H5ToXds is provided only for Linux
(as of 2016 Feb). In this case, you may want to create the following
wrapper script named H5ToXds.

```bash
#!/bin/sh

# redirect STDERR to /dev/null so that it does not disturb console outputs from other programs.
/path/to/eiger2cbf $@ 2>/dev/null
```

Performance Considerations
---------------------------

I believe that eiger2cbf is fast enough for offline processing but if
you are impatient, you might want to write a small wrapper script to
launch multiple instances of eiger2cbf for each subset of a dataset
(e.g. image 1 to 100, 101 to 200 and so on). Probably disk and/or
network IO will be the next bottleneck.

Online processing on a high performance storage is another story.
Here, eiger2cbf can be a bottleneck. If there is really a demand,
I am happy to help. Post a feature request on the Issues page.

Alternative choices
-------------------

H5ToXds from Dectris and hdf2mini-cbf from Global Phasing have similar
capacities. The advantage of eiger2cbf is that it is open-source, free
software. The others are closed-source. MOSFLM cannot process outputs
from H5ToXds because they lack essential headers.

Conversion details
------------------

The program reads the pixel mask from the master H5 file and apply it to
the image. When the mask is 1, it outputs -1. When the mask is 2, 4, 8 or 16,
it outputs -2. Otherwise, the program outputs the original value as is.

If the pixel mask is not available, the program assumes that the pixel mask
has already been applied to the images. The values of the invalid pixels
(65535 if the image is 16-bit) are converted to -1 (as in Pilatus).
However, this might also mask heavily saturated pixels. Fortunately,
most datasets contain the pixel mask in the master H5 file.

The following metadata are converted from the master h5.

-   Detector name and serial number
-   Pixel size
-   Sensor thickness
-   Countrate correction cutoff
-   Wavelength
-   Detector distance
-   Beam center
-   Exposure time
-   Exposure period
-   Start angle
-   Angle increment

Unfortunately, some of them are missing in some datasets. In these cases, 
the converter outputs zero or standard (common) values. See the console output.
In the future, we will add command-line options to supply metadata.

Warning: currently, we assume the rotation is around the 'omega' axis and
two-theta is 0. Send me test data if you need support for more complex geometry.

Support
-------

The program has been tested on the following datasets.

-   EIGER 16M at SLS X06SA (2016 Feb)
-   EIGER 4M at ESRF ID30A-3
-   EIGER 4M and 16M sample datasets on Dectris website (downloaded in 2015 Nov)
-   EIGER 9M at SPring-8 BL32XU (2016 May)

As there are many beamline and firmware-version specific
"dialects", eiger2cbf might not work on your dataset.
If the program does not work well, feel free to contact me
from the "Issues" page. If you can give me test datasets, it 
would be very helpful!

Acknowledgements
----------------

I thank [Harry Powell](http://www.mrc-lmb.cam.ac.uk/harry/) for writing Makefile,
fixing compiler warnings and building & hosting binaries. 
I thank those who gave me feedback and test datasets.

LICENSE
-------

BSD license. 

eiger2cbf includes [bitshuffle](https://github.com/kiyo-masui/bitshuffle) and 
[lz4 HDF5 plugin](https://github.com/dectris/HDF5Plugin), both of which are also
licensed under the BSD license. See LICENSE files in bitshuffle and lz4
directories.

eiger2cbf also depends on [HDF5 library](https://www.hdfgroup.org/HDF5/) and 
[CBFlib](http://www.bernstein-plus-sons.com/software/CBF/). 
See LICENSE.HDF5 and LICENSE.CBFlib. Both are not included in this source distribution.
