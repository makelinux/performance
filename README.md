# Performance and throughput measurement utility

Disk and FS throughput measurement utility with accuracy
(standard deviation) calculation

Motivation
* dd is too simple and not script friendly.
* fio is too complex and not script friendly.
* The accuracy of results is unknown.
* Utilities must be run multiple times to achieve reliable results.

**Features:**

## Simplicity

Simple command line interface, simple usage and simple implementation on C.

## Reliability

Calculates standard deviation of mean with function gsl_rstat_sd_mean from GNU Scientific Library.
The measurements continue until standard deviation is less than specified stdev_percent as a percentage
from the mean value. The default value of stdev_percent is 10%.

## Batch scripting friendly

Intermediate and final standard deviation are printed to stderr to
simplify usage of throughput results.

Option --quiet suppresses intermediate data and gives only final results.
Option --batch prints only numbers in KiB/s, ready for integer arithmetic

Sample scripting usage:
```
a=($(throughput --quiet --batch 2>&1))
echo "Overall (mean) throughput: ${a[0]}"
echo "Standard deviation of mean: ${a[1]}"
```
or

```
TP=(throughput --quiet --batch 2> /dev/null)
```

Integration with git bisect:

test $(throughput --batch --quiet 2> /dev/null) -gt 100 && git bisect good || git bisect bad

## Comparison mode

When two arguments are given, the tools measures both throughputs and calculates
absolute and percents change.

## Other features
* Multithreaded mode
* Read and write mode
* directory, file and block device target

## Compare usage with other tools

The following sample groups have similar parameters:

```bash
./throughput
fio --name=t --bs=128M --size=1280M --fdatasync=1 --direct=1 --numjobs=1
ioping -i 0 -c 2 -s 128M -c 10  .

./throughput --write
fio --name=t --rw=write --bs=128M --size=1280M --fdatasync=1 --direct=1 --numjobs=1

./throughput --threads=10
fio --name=test --rw=write --bs=128M --size=128M --fdatasync=1 --direct=1 --numjobs=10

sudo ./throughput /dev/sda
sudo hdparm -t /dev/sda

```

## Notes

* requires packages libgsl-dev and gnulib
