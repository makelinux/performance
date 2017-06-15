# Performance and throughput measurement utility

Disk and FS throughput measurement utility with accuracy
(standard deviation) calculation

Motivation:
* dd is too simple and not scripting friendly
* fio is too complex and not scripting friendly too
* The accuracy of results is unknown.
* And it is need waste a lot of time to run utilities to achieve reliable results.

## Simplicity

Simple command line interface, simple usage and simple implementation on C.

## Reliability

Calculates accuracy estimation by formula:

A = (max - min) / (4 * sqrt(N - 1))

Accordingly Range rule for standard deviation and Standard error of the mean.

## Batch scripting friendly

Intermediate and finale standard deviation are prints to stderr to
simplify usage of throughput results.

Option --quiet suppresses intermediate data and gives only final results.
Option --batch prints only numbers in KB/s, ready for integer arithmetic

Sample scripting usage:
```
a=($(throughput --quiet --batch 2>&1))
echo Overall (mean) throughput: ${a[0]}
echo Standard deviation of mean: ${a[1]}
```
or

```
TP=(throughput --quiet --batch 2> /dev/null)
```

## Comparison mode

When two arguments are given, the tools measures both throughputs and calculates
range of change in precents, where 0% is no change.
