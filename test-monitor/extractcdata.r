# Set the working directory
source("funcs.R")

setwd("./container/pretest/")
options("width"=200)
library(ggplot2)

#types <- c("test1", "test2", "test3", "test4")
#types <- c("test8", "test5", "test6", "test7")
#types <- c("test9", "test10", "test11", "test12")
types <- c("test2", "test8", "test9")

sink("containerstats.txt")

for (i in 1:length(machines)) {
	for (l in 1:length(tests)) {

		mplot <- data.frame("Type"=character(),"Test"=character(),"Mdn"=numeric(),"Avg"=numeric())
					
		for (k in 1:length(tests[[l]])) {
			testPcount = 0;
			tplot <- data.frame()

			for (j in 1:length(types)) {


				# Label and directory pattern
				cat(machines[i], "-", types[j], "-", tests[[l]][[k]], "\n")
				
				# Find all directories with pattern.. 
				dir = paste0(machines[i], "/", types[j], "/", tests[[l]][[k]])

				maxAll = 0
				pcountAll = 0

				r<- data.frame (matrix(ncol=13,nrow=0))
				names(r) <- c("Min", "Mdn", "Avg", "runDif","runStD", "Max", "pMin", "pMdn", "pAvg", "pavgDev", "pMax", "pOVPeak", "pcount")
				plot <- data.frame()
				nr = 0
				files <- list.files(path=dir, pattern="*.log", full.names=TRUE, recursive=FALSE)

				# Load Container result file of this experiment, one file per container
				for (x in files) {

					dat <- loadData(x) # load log file of experiment, container

					datp <- dat[!(dat$RunT > dat$cDur*1.5),] # Dump all values higher than 1.5*  programmed duration (filter of overshoots for datp)

					minMin = min(datp$RunT)
					avgMea = mean(datp$RunT)
					avgMed = median(datp$RunT)
					runStD = sqrt(var(datp$RunT))
					runDif = avgMed-avgMea;
					maxMax = max(datp$RunT)
					pminMin = min(datp$Period)
					pavgMea = mean(datp$Period)
					pavgMdn = median(datp$Period)
					pavgDev = sqrt(var(datp$Period))
					pmaxMax = max(datp$Period)
					pmaxMaxp = max(dat$Period)
					pcount = sum ( dat$Period > pavgMea*1.5)
					maxAll = max(maxMax, maxAll)
					pcountAll = pcountAll + pcount

					r[nrow(r)+1,] <-data.frame (Min=minMin, Mdn=avgMed, Avg=avgMea, runDif=runDif, runStD=runStD, Max=maxMax,
						pMin=pminMin, pMdn=pavgMdn, pAvg=pavgMea, pAvgDev=pavgDev, pMax=pmaxMax, pOVPeak=pmaxMaxp, pcount)
					plot <- rbind(plot, dat)
				}
				# Process experiment totals 
				plot$type <- types[j]
				plot$oversh <- pcountAll
				mplotMed = median(plot$RunT) # median  on all experiments of set
				mplotAvg = mean(plot$RunT) # average on all experiments of set
				mplot <- rbind(mplot, data.frame (Type=types[j], Test=tests[[l]][[k]], Mdn=mplotMed, Avg=mplotAvg))

				# Bind to total result
				tplot<-rbind(tplot,plot)
				print(r)
				cat ("Peak - Peak count ", maxAll, pcountAll, "\n")
				testPcount = testPcount + pcountAll
	  		}

	  		# plot single test
	  		head(tplot)
	  		pdf(file= paste0(machines[i],"_" , tests[[l]][[k]],".pdf"), width = 10, height = 10)

			tplot <- tplot[!((tplot$RunT > tplot$cDur*1.5)),] # drop exceeding points
	  		hist <- ggplot(tplot, aes(x=type, y=RunT), col = rainbow(7)) + 
				scale_y_continuous(limits=quantile(tplot$RunT, c(0.1,0.9))) +
			 	geom_boxplot(fill="slateblue", alpha=0.2, outlier.shape=NA) +
			    labs(x="Test type", y= expression(paste("Run-time  (values are in ", mu, "s)")), size=">10 ms in\n% of set")+
				scale_fill_viridis_d() 
	  		print (hist)
	  		dev.off()

			#		scale_y_continuous(trans='log10') +
	  		# 		ylim (c(tplot$RunT[1]*0.995,tplot$RunT[1]*1.005)) + 
	  		#		xlim (c(tplot$RunT[1]*0.995,tplot$RunT[1]*1.005)) + 
			#	    geom_point(data = tplot, aes(x = type, y = tplot$cDur*1.5, size=oversh), shape=17, , color="red", fill="red") +

	        # head(tplot)
	        # pdf(file= paste0(machines[i],"_" , tests[l,k],".pdf"), width = 10, height = 10)
	        # hist <- ggplot(tplot, aes(x=RunT, fill = types), col = rainbow(7)) + 
	        #         geom_density(alpha = 0.1) +
	        #         xlim (c(tplot$cDur[1]*0.95,tplot$cDur[1]*1.05)) + 
	        #         ylab("Count") +
	        #         scale_fill_viridis_d()
	        # print (hist)
	        # dev.off()

			cat ("Test set overruns ", testPcount , "\n")
			cat ("----------------------------------------\n")
		}
		pdf(file= paste0(machines[i], "_", l ,"_median.pdf"), width = 10, height = 10)
		plot1 <- ggplot(mplot, aes(x=Test, y=Mdn, group=Type, color=Type), col = rainbow(7)) + 
			geom_line(aes(linetype=Type))+
	  		geom_point()+
		    labs(x="Test type", y= expression(paste("Run-time  (values are in ", mu, "s)")))+
			scale_fill_viridis_d() 
		print (plot1)
		dev.off()

		pdf(file= paste0(machines[i], "_", l ,"_average.pdf"), width = 10, height = 10)
		plot1 <- ggplot(mplot, aes(x=Test, y=Avg, group=Type, color=Type), col = rainbow(7)) + 
			geom_line(aes(linetype=Type))+
	  		geom_point()+
		    labs(x="Test type", y= expression(paste("Run-time  (values are in ", mu, "s)")))+
			scale_fill_viridis_d() 
		print (plot1)
		dev.off()
	}
}
