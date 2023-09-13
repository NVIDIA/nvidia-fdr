IMAGE=nvidia-fdr-coverage
TAG=20230904

cd $(dirname $0)

docker build -t $IMAGE:$TAG .
docker run -it --rm -v `pwd`/../:/nvidia-fdr --privileged -v /run:/run  $IMAGE:$TAG $@