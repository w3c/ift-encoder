To update these subsets after an update to harfbuzz:

hb-subset testdata/Roboto-Regular.ttf --text=<subset> --output-file=testdata/Roboto-Regular.<subset>.ttf --drop-tables-=GPOS,GSUB,GDEF --retain-gids
