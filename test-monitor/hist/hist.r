
library(ggplot2)
library(reshape2)


df = read.csv("stats.csv", TRUE, ";")[1:10000,1:9]
head(df)
df.long <- melt(df, id = "Time", measure = c("T3.Aws", "T3.Xen", "T3.Xen.U", "T3.Prt", "T3.Prt.U", "C5.Aws","C5.Xen","C5.Prt"))
#df.long <- melt(df, id = "Time", measure = c("T3.Aws", "T3.Xen", "T3.Xen.U", "T3.Prt", "T3.Prt.U"))
#df.long <- melt(df, id = "Time", measure = c("C5.Aws","C5.Xen","C5.Prt"))

ggplot(df.long, aes(x=Time, value, color=variable)) +
	#xlim (c(0,100)) + 
	ylab("Count") + 
	scale_x_continuous(trans='log10') +
	scale_y_continuous(trans='log10') +
  geom_line()
