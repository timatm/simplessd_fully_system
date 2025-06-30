# BUILD: `docker build --build-arg UID=$(id -u) --build-arg GID=$(id -g) -f $THIS_FILE -t $IMAGE_NAME .`
# USAGE: `docker run --name simplessd-env -v $HOST_SIMPLESSD_PATH:/ws -w /ws -u $(id -u):$(id -g) -it $IMAGE_NAME tmux`

FROM ubuntu:18.04

ARG UID=1000
ARG GID=1000

ENV DEBIAN_FRONTEND=noninteractive

# gem5 base system
RUN apt -y update && apt -y upgrade
RUN apt -y install build-essential scons python-dev zlib1g-dev m4 cmake libprotobuf-dev protobuf-compiler libgoogle-perftools-dev git python-pip
RUN apt -y install bc cmake e2fsprogs libext2fs-dev libzstd-dev libpcre++-dev wget
# additional libs
RUN apt -y install libzstd-dev xxd

# Create a user named `user` with sudo permission
RUN apt -y install sudo nano
RUN groupadd -g $GID -o user
RUN useradd -m -o -u ${UID} -g ${GID} -s /bin/bash user
RUN echo "user:user" | chpasswd
RUN echo "user    ALL=(ALL:ALL) ALL" >> /etc/sudoers

# 安裝 CMake 3.26.4（或其他你想要的版本）
RUN wget https://github.com/Kitware/CMake/releases/download/v3.26.4/cmake-3.26.4-linux-x86_64.sh -O /tmp/cmake-install.sh \
 && chmod +x /tmp/cmake-install.sh \
 && /tmp/cmake-install.sh --skip-license --prefix=/usr/local \
 && rm /tmp/cmake-install.sh


# TMUX for multiple terminals in docker (https://tmuxcheatsheet.com/)
RUN apt -y install tmux
RUN su user
RUN echo "set-option -g default-shell /bin/bash" > $HOME/.tmux.conf