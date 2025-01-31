# QEMU

To run qemu you must have the following packages installed:
```
$ sudo apt-get install -y qemu-system-arm 
```

Before starting work, the network environment must be set up by running the following script:

```
$ bazel run @srp_platform//tools/qemu:network_configure 
```

We can then run qemu simulations via the following command:

```
$ bazel run @srp_platform//tools/qemu
```

In the above case, the console will be launched in a separate window. Requires a graphics driver.
To run without graphics drivers, use the following command. It will launch qemu in the terminal. * CTRL+X will not work.

```
bazel run @srp_platform//tools/qemu -- --console
```