#!/bin/sh 
echo "Generate coverage badge - (run from project root!)"
perc=$(gcov -no build */*/*.[hc] 2>&1 | tail -n 1 | grep -o '[0-9]*' | head -n1)
echo Coverage is $perc

# replace text and text shadow
sed -i '/<text x="658" y="148"/s/>.*</>'${perc}%'</' test/coverage.svg
sed -i '/<text x="648" y="138"/s/>.*</>'${perc}%'</' test/coverage.svg

# update fill color
if [ $perc -ge 90 ]; then
	# good coverage -> green
	sed -i '/<rect width="430"/s/fill=".*"\ /fill="lightgreen"\ /' test/coverage.svg
elif [ $perc -ge 80 ]; then
	# medium coverage -> yellow
	sed -i '/<rect width="430"/s/fill=".*"\ /fill="greenyellow"\ /' test/coverage.svg
	
elif [ $perc -ge 70 ]; then
 	# some coverage -> orange
	sed -i '/<rect width="430"/s/fill=".*"\ /fill="orange"\ /' test/coverage.svg
 	
else
	# less than 70 % -> RED
	sed -i '/<rect width="430"/s/fill=".*"\ /fill="orangered"\ /' test/coverage.svg
fi
