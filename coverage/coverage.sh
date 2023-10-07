IMAGE=nvidia-fdr-coverage
TAG=20230922

cd $(dirname $0)

docker inspect $IMAGE:$TAG > /dev/null || docker build --build-arg UNAME=$(whoami) --build-arg UID=$(id -u) --build-arg GID=$(id -g) -t $IMAGE:$TAG .
docker run -it --rm -v `pwd`/../:/nvidia-fdr --privileged -v /run:/run  $IMAGE:$TAG $@