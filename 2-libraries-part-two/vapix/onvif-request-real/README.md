# Request to onvif media service with dbus credentials retrival

## Try to modify a specific onvif stream profile with enpoint 

<pre><code>http://ip/onvif/services</code></pre>

## Build the application: 

<pre><code>docker build --build-arg ARCH=aarch64 --tag onvif-request-real .</code></pre>

## Copy the result from the container image to a local directory build:

<pre><code>docker cp $(docker create onvif-request-real):/opt/app ./build</code></pre>

## The expected output:

------


## Application log can be found directly at:

<pre><code>http://ip/axis-cgi/admin/systemlog.cgi?appname=onvif-request-real</code></pre>