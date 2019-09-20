#/bin/bash
srcDir=./src
dockerDir=./docker
cd $srcDir

make
cp -f WorkerApp "../$dockerDir/workerapp"
cp -f DataGenerator "../$dockerDir/datagenerator"

cd "../$dockerDir"

echo "executables:"
ls -l datagenerator workerapp
echo

echo "Stop all running workerapp containers"
docker rm $(docker stop $(docker ps -a | egrep -e 'datagenerator|datadistributor|workerapp' | awk '{print $1}'))

for (( i=0; i<10; i++ )); do
    docker image rm -f rt-workerapp$i
done

docker image rm -f rt-datagenerator
docker image rm -f rt-datadistributor
docker system prune -f

echo "Docker images before build:"
docker images
echo

for (( i=0; i<10; i++ )); do
    docker build -f ./Dockerfile.wa$i -t rt-workerapp$i .
done

docker build -f ./Dockerfile.dg -t rt-datagenerator .
docker build -f ./Dockerfile.dd -t rt-datadistributor .

echo "Docker images after build:"
docker images

cd ..