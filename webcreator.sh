#!/bin/bash
echo "# Web Creator..."
declare -A webpages
declare -A included
declare -A internal
declare -A external

if ! [ -d $1 ] ; then
	echo "# Root directory given isn't an actual directory"; exit 1
fi

if ! [ -e $2 ] ; then
	echo "# Text file given isn't a regular file"; exit 1
fi

if ! [ -r $2 ] ; then
	echo "# I don't have read rights in the text file given"; exit 1
fi

let lines=`wc -l < $2`;

in='^[0-9]+$'

if ! [[ $3 =~ $in ]] ; then
	echo "# Error: Third argument is not a number"; exit 1
fi

if ! [[ $4 =~ $in ]] ; then
	echo "# Error: Fourth argument is not a number"; exit 1
fi

echo "# Correct format. Proceeding to site creation."

cd $1

if test "$(ls)"; then
	echo "# Warning: directory is full, purging ..."
	rm -r *
fi

if [[ $3 -le 1 ]]; then
	echo "# Error: w must be greather than 1. "; exit 1
fi

if [[ $4 -le 1 ]]; then
	echo "# Error: p must be greather than 1. "; exit 1
fi

let x=$3-1;
for i in `seq 0 $x`; do
	mkdir "site$i"
done

let y=$4-1
for i in $(seq 0 $x); do
	for j in $(seq 0 $y); do
		random=$RANDOM
		webpages[$i,$j]="site$i/page$i``_$random.html"
		touch "site$i/page$i``_$random.html"
		included[$i,$j]=0
	done
done

let x=`expr $3/2`;
let y=`expr $4/2`;
let j=0;
path=`pwd`;

for dirname in *; do
	echo "# Creating web site $j"
	cd $dirname
	let p=0;
	for filename in *; do
		let k=$((1 + RANDOM % ($lines - 2000)));
		let m=$((1001 + RANDOM % 2000));
		echo "#  Creating page $1/site$j/$filename with `expr $lines - $k` lines starting at line $k "
		for i in `seq 0 $x`; do
			let randomsite=$((RANDOM % $3))
			let randompage=$((RANDOM % $4))
			while [ "$randomsite" -eq "$j" ]
			do
				let randomsite=$((RANDOM % $3))
			done
			external[$i]="../${webpages[$randomsite,$randompage]}"
			included[$j,$randompage]=1
		done
		for i in `seq 0 $y`; do
			let randompage=$((RANDOM % $4))
			while [ "$dirname/$filename" = "${webpages[$j,$randompage]}" ]
			do
				let randompage=$((RANDOM % $4))
			done
			internal[$i]="../${webpages[$j,$randompage]}"
			included[$j,$randompage]=1
		done
		echo "<!DOCTYPE html>" >> "$filename"
		echo "<html>" >> "$filename"
		echo "	<body>" >> "$filename"
		let limit=`expr $m/$(($x + $y))`
		let counter=$limit+1
		let int=0
		let ext=0
		let offset=0
		cd ..
		cd ..
		while IFS='' read -r line || [[ -n "$line" ]]; do
			let "offset++"
			if [[ $offset -gt $limit ]] ; then
				echo "$line <br>" >> "$1/$dirname/$filename"
				if [[ offset -eq  $counter ]] ; then
					if [[ "$int" -le "$y" ]] ; then
						echo "#   Adding link to ${internal[$int]}"
						echo "<a href="${internal[$int]}">Internal Link</a> <br>" >> "$1/$dirname/$filename"
						let "int++"
					elif [[ "$ext" -le "$x" ]] ; then
						echo "#   Adding link to ${external[$ext]}"
						echo "<a href="${external[$ext]}">External Link</a> <br>" >> "$1/$dirname/$filename"
						let "ext++"
					fi
					let counter=$counter+$limit
				fi
			fi
		done < $2
		echo "	</body>" >> "$1/$dirname/$filename"
		echo "</html>" >> "$1/$dirname/$filename"
		let int=0
		let ext=0
		cd $1
		cd $dirname
		let "p++"
	done
	cd ..
	let "j++"
done

let x=$3-1;
let y=$4-1;
let flag=1;
for i in `seq 0 $x`; do
	for j in `seq 0 $y`; do
		if [[ ${included[$i,$j]} -eq 0 ]] ; then
			echo "# Not all pages have at least one incoming link"
			let flag=0
			break
		fi
	done
	if [[ "$flag" -eq 0 ]] ; then
		break
	fi
done

if [[ $flag -eq 1 ]] ; then
	echo "# All pages have at least one incoming link"
fi

echo "# Done."	
