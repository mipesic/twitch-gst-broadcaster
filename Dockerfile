#Base image
FROM restreamio/gstreamer:1.18.4.0-prod
RUN apt-get update && apt-get install -y cmake pkg-config libglib2.0 libunwind-dev libdw-dev

COPY . .

RUN mkdir build && cd build && cmake -DBUILD_TYPE=Debug .. && make
RUN cd build && ./dyn_video_pipeline
RUN cd build && ./dyn_video_pipeline_tests