
# Set the working directory
setwd(".")
datacnt = 13


loadData <- function(fName) {
	#Function, load data from text file into a data frame, only min, avg and max

	# Read text into R
	#cat (fName, "\n")

	if (!file.exists(fName)) {
		# break here if file does not exist
		dat <- data.frame(Min=numeric(), Avg=numeric(), Max=numeric())
		return(dat)
	}

	# Read text file remove everything except numbers
	tFile <- readLines(fName)
	tFile <- (gsub("[a-zA-Z:()]", "", tFile))

	# Remove comments, fill headers in new dataset
	rep_date_entries = grep("^#", tFile)
	tFile <- tFile[-rep_date_entries]

	# Transform into table, filter and add names
	dat = read.table(text = tFile)
	dat <- data.frame ("Min"=dat$V6,"Avg"=dat$V8,"Max"=dat$V9)
	
	return(dat)
}

machines <- c("LT", "BM" , "T3", "C5")
types <- c("Std", "Xen", "Prt")

tests <- c("NoIsoNoLoad", "NoIsoLoad", "IsoNoLoad", "IsoLoad", "IsoNoBalNoLoad", "IsoNoBalLoad", "IsoNoBalIRQNoLoad", "IsoNoBalIRQLoad","IsoGNoLoad", "IsoGLoad", "IsoNoBalGNoLoad", "IsoNoBalGLoad", "IsoNoBalIRQGNoLoad", "IsoNoBalIRQGLoad", "IsoLbnNiNoBalGNoLoad", "IsoLbnNiNoBalGLoad", "IsoLbNoBalGNoLoad", "IsoLbNoBalGLoad", "IsoLbnNiIsoGNoLoad", "IsoLbnNiIsoGLoad")
#tests <- c("NoIsoNoLoad", "NoIsoLoad", "IsoNoLoad", "IsoLoad", "IsoNoBalNoLoad", "IsoNoBalLoad", "IsoNoBalIRQNoLoad", "IsoNoBalIRQLoad")
#lttests <- c("IsoGNoLoad", "IsoGLoad", "IsoNoBalGNoLoad", "IsoNoBalGLoad", "IsoNoBalIRQGNoLoad", "IsoNoBalIRQGLoad", "IsoLbnNiNoBalGNoLoad", "IsoLbnNiNoBalGLoad", "IsoLbNoBalGNoLoad", "IsoLbNoBalGLoad")

results <- list()

for (k in 1:length(tests)) {


	size = length(machines) * length(types) # size of all 
	minMin  <- array( 0, dim = c(size)) # holds min mean of all
	avgMea <- array( 0, dim = c(size))  # holds avg mean of all
	avgDev <- array( 0, dim = c(size))  # holds avg deviation of all
	maxMax <- array( 0, dim = c(size))  # holds max mean of all
	label  <- array( 0, dim = c(size)) # holds test label
	p = 0 # actual array position

	for (i in 1:length(machines)) {
		for (j in 1:length(types)) {
	
			p = p + 1
			# Label and directory pattern
			label[p] = paste0(machines[i], "-", types[j])
			
			# Find all directories with pattern.. 
			dirs <- dir(".", pattern= paste0("^", label[p]))

			# Combine results of test batches
			dat <- data.frame(Min=numeric(), Avg=numeric(), Max=numeric())
			for (d in seq(along=dirs)){

				fName <- paste0(dirs[d], "/", tests[k], "-res.txt")
				#cat (fName, "\n")
				pt <- loadData(fName)
				dat <- rbind(dat,pt)

			}
			if ( nrow(dat)<datacnt ) {
				cat ("Error data: ", label[p], "-", tests[k], ", count", nrow(dat), "\n")
			}

			minMin[p] = min(dat$Min)
			avgMea[p] = mean(dat$Avg)
			avgDev[p] = sqrt(var(dat$Avg))
			maxMax[p] = max(dat$Max)
		}
	}

	results[[k]] <- data.frame (label, minMin, avgMea, avgDev, maxMax)
	cat("Test" , tests[k], "\n")
	print(results[[k]])
	cat("\n")
}

dat <- loadData("C5-Prt-Cont")
results[[k+1]] <- data.frame ("Container", min(dat$Min), mean(dat$Avg), sqrt(var(dat$Avg)), max(dat$Max))
print(results[[k+1]])

dat <- loadData("C5-Prt-Cont-load")
results[[k+2]] <- data.frame ("Container", min(dat$Min), mean(dat$Avg), sqrt(var(dat$Avg)), max(dat$Max))
print(results[[k+2]])
