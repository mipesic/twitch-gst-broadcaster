# Twitch broadcaster
Proof of concept of a media pipeline which takes three media sources, mixes their audio and video tracks and streams the content over rtmp. Could be used for [twitch broadcasting](https://help.twitch.tv/s/article/broadcast-guidelines?language=en_US).
It's using gstreamer library (1.18.4).


# How to build

The repo contains docker file which builds the solutions and runs tests. To build the image make sure you have docker installed and run:
```
docker build -f Dockerfile .
```
That's going to build the solution and run existing tests. After building the image you can run the docker container and mount storage (containing sample files). With that the app could be run within container.

Alternatively the solution could be built locally (on a dev machine). Make sure gstreamer and all plugins are installed and run e.g:
```
mkdir build && cd build && cmake -DBUILD_TYPE=Debug .. && make
```
to build the targets.

# How to run
Once targets are built you can check which options are needed:
```
./dyn_video_pipeline --help
```
which prints:
```
Help Options:
  -h, --help              Show help options

Application Options:
  -f, --filesink=         Optional file sink location, if present overwrites rtmp
  -1, --source1=          Source 1 location
  -2, --source2=          Source 2 location
  -3, --source3=          Source 3 location
  -r, --rtmp_address=     rtmp ddress
```
An example of invoking it;
```
./dyn_video_pipeline \
	-1 https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm \
	-2 https://dl8.webmfiles.org/big-buck-bunny_trailer.webm \
	-3 https://dl8.webmfiles.org/elephants-dream.webm \
	-r "rtmp://127.0.0.1/live live=true"

#Testing
ffplay rtmp://127.0.0.1/live/
```
When specifying local files use `file://`instead of `http(s)://`



To run tests:
```
./dyn_video_pipeline_tests
```
# Output
The result is 1080p video (h264 encoded) which contains all 3 input video tracks mixed and positioned one next to the other across x-axis. Original aspect ratio should be preserved. Audio tracks are also mixed (AAC encoded).

## Pipeline structure
The media pipeline graph when in playing state is added to the repo: `playing_pipeline.png` so if you are interested which gstreamer components are used and how they are connected at glance, you can check the graph.

# Limitations (ideas for improvements)
- **Testing!** - the solution has been tested only with a very limited set of input files. More diverse input based testing would be good. Automation checks in place are very rudimentary, resulting media format checks are need. Although during local tests, no performance issues were visible - more performance testing would be needed.
- To simplify initial version of the implementation - the solution assumes only one audio and only one video track per source. They could contain more but the pipeline will only accept the first 3 audio and the first 3 video tracks detected. Also there is no support for reconfiguring pipeline if a track disappears in the middle of the source data. That's something that could be handled more dynamically.
- Encoding params tweaking (both audio & video). Didn't have time to make sure to set all params according to the twitch spec mentioned above.
- Solution could be extended to support different & configurable output resolutions as well as configurable layouts for the resulting mix.
