#/bin/bash
srcDir=./src
dockerDir=./docker
cd $srcDir

# Build binaries and copy them to the docker build directory
make
cp -f WorkerApp "../$dockerDir/workerapp"
cp -f DataGenerator "../$dockerDir/datagenerator"

cd "../$dockerDir"

echo "executables:"
ls -l datagenerator workerapp
echo

# Stop containers and cleanup
echo "Stop all running workerapp containers"
docker rm $(docker stop $(docker ps -a | egrep -e 'datagenerator|datadistributor|workerapp' | awk '{print $1}'))

for i in {0..9}
do
    docker image rm -f rt-workerapp$i
done

docker image rm -f rt-datagenerator
docker image rm -f rt-datadistributor
docker system prune -f

echo "Docker images before build:"
docker images
echo

# Rebuild all docker images for the use case
for i in {0..9}
do
    docker build -f ./Dockerfile.wa$i -t rt-workerapp$i .
done

docker build -f ./Dockerfile.dg -t rt-datagenerator .
docker build -f ./Dockerfile.dd -t rt-datadistributor .

echo "Docker images after build:"
docker images

cd ..