# OS/161

The OS/161 operating system, which runs on the Sys/161 VM. For CS 350 by Nikolai Frasser, University of Waterloo

## Setup

* [OS/161 Installation Guide for the student.cs computing environment](https://www.student.cs.uwaterloo.ca/~cs350/common/Install161.html)
* [OS/161 Installation Guide for other machines](https://www.student.cs.uwaterloo.ca/~cs350/common/Install161NonCS.html)
* [Working with OS/161](https://www.student.cs.uwaterloo.ca/~cs350/common/WorkingWith161.html)
* [Debugging OS/161 with GDB](https://www.student.cs.uwaterloo.ca/~cs350/common/gdb.html)
* [OS/161 and tools FAQ](https://www.student.cs.uwaterloo.ca/~cs350/common/os161-faq.html)
* Manuals and Code
  * [Browseable OS/161 source code](https://www.student.cs.uwaterloo.ca/~cs350/common/os161-src-html)
  * [Browseable OS/161 man pages](https://www.student.cs.uwaterloo.ca/~cs350/common/os161-man)
  * The [sys161 manual](https://www.student.cs.uwaterloo.ca/~cs350/common/sys161manual). This information is also available in the sys161 source code distribution.

### Setting up to work on an assignment

For the root project directory, run the following commands (replace the `x` in `ASSTx` with whichever assignment you're working on).

```sh
./configure --ostree=$HOME/cs350-os161/root --toolprefix=cs350-
cd kern/conf
./config ASSTx
cd ../compile/ASSTx
bmake depend
bmake
bmake install
```

### Build the OS/161 User-level Programs

Build the OS/161 user level utilities and test programs (from root project directory):

```sh
bmake
bmake install
```

## Running OS/161

Navigate into the automatically-generated folder called `root` in the same directory as this repository. Copy the file `~/sys161/sys161.conf` into this directory.

Then run

```
sys161 kernel-ASSTx
```

Replacing `x` with whatever assignment you're working on. Enter the `q` command to quit when you're finished.

## Submitting Your Work and Checking Your Submission

* [Overview of `cs350_submit`](http://www.student.cs.uwaterloo.ca/~cs350/common/SubmitAndCheck.html) - how to submit your work and check that it will compile for marking.
