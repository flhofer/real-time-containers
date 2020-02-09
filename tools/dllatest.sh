#!/usr/bin/env bash
#
# inspired by gh-dl-release!
# 
# This script downloads the artifacts from the latest successful build
# of private GitHub repositories.

TOKEN="<github-private-token>"
REPO="flhofer/real-time-containers"
GITHUB="https://api.github.com"
WFILE="cicd.yml"

# generic extension of call with auth token header
function gh_curl() {
  curl -H "Authorization: token $TOKEN" \
       -H "Accept: application/vnd.github.v3.raw" \
       $@
}

# Get latest successful run id
echo "Fetching latest run-id"
parser=".workflow_runs | map(select(.conclusion == \"success\")) | .[0].id"
run_id=`gh_curl -s $GITHUB/repos/$REPO/actions/workflows/$WFILE/runs | jq "$parser"`
if [ "$run_id" = "" ]; then
  >&2 echo "ERROR: run-id not found for workflow $WFILE"
  exit 1
fi;

# get json with artifact identifications and urls
echo "Fetching artifact urls"
parser=".artifacts | .[] | .archive_download_url"
artifacts=`gh_curl -s $GITHUB/repos/$REPO/actions/runs/$run_id/artifacts`
artifact_total=`echo $artifacts | jq ".total_count"`
artifact_dl=`echo "$artifacts" | jq "$parser"`
if [ "$artifact_dl" = "" ]; then
  >&2 echo "ERROR: artifact urls not found for run-id $run_id"
  exit 1
fi;

echo "Downloading artifacts of run-id $run_id.."
i=0
# loop through artifact lines and store the zips
while IFS= read -r artifact_dl; do
  if [[ artifact_total -gt 1 ]]; then
    tfile=art$i
  else
    tfile=artifacts
  let i++
  echo "storing to $tfile.zip the contents of $artifact_dl"
  eval curl -v -L -u octocat:$TOKEN -o $tfile.zip $artifact_dl
  # Extract
  echo "Extract archive $tfile.zip.."
  eval unzip $tfile.zip -d $tfile
done <<< "$artifact_dl"
