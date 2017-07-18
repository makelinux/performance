# Performance and throughput measurement utility

Disk and FS throughput measurement utility with accuracy
(standard deviation) calculation

Motivation:
* dd is too simple and not scripting friendly
* fio is too complex and not scripting friendly too
* The accuracy of results is unknown.
* And it is need to waste a lot of time to run utilities to achieve reliable results.

**Features:**

## Simplicity

Simple command line interface, simple usage and simple implementation on C.

## Reliability

Calculates standard deviation of mean with function gsl_rstat_sd_mean from
GNU Scientific Library. The measurements continues till standard deviation is
less than specified stdev_percent in percents from the mean value.
The default value of stdev_percent is 10%.

## Batch scripting friendly

Intermediate and final standard deviation are printed to stderr to
simplify usage of throughput results.

Option --quiet suppresses intermediate data and gives only final results.
Option --batch prints only numbers in KB/s, ready for integer arithmetic

Sample scripting usage:
```
a=($(throughput --quiet --batch 2>&1))
echo "Overall (mean) throughput: ${a[1]}"
echo "Standard deviation of mean: ${a[0]}"
```
or

```
TP=(throughput --quiet --batch 2> /dev/null)
```

## Comparison mode

When two arguments are given, the tools measures both throughputs and calculates
absolute and percents change.

## Other features
* Multithreaded mode
* Read and write mode
* directory, file and block device target

## Compare usage with other tools

Folowing samples groups have similar parameters

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

* requires package libgsl-dev
