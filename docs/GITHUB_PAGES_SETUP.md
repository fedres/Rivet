# GitHub Pages Setup Guide

## Steps to Make Your Site Live

### 1. Go to Repository Settings
1. Open https://github.com/fedres/Rivet in your browser
2. Click on **Settings** (gear icon in the top menu)

### 2. Navigate to Pages Section
1. In the left sidebar, scroll down and click **Pages**
2. You should see "GitHub Pages" settings

### 3. Configure Source
Under "Build and deployment":

**Option A: Deploy from Branch (Recommended)**
1. Under "Source", select **Deploy from a branch**
2. Under "Branch":
   - Select `production` from the dropdown
   - Select `/ (root)` for the folder (NOT /site)
   - Click **Save**

**Option B: If you want to deploy from /site folder**
1. Under "Source", select **Deploy from a branch**
2. Under "Branch":
   - Select `production`
   - Select `/site` from the folder dropdown
   - Click **Save**

### 4. Wait for Deployment
- GitHub will start building your site
- This usually takes 1-2 minutes
- You'll see a message: "Your site is live at https://fedres.github.io/Rivet/"

### 5. Check Deployment Status
1. Go to the **Actions** tab in your repository
2. Look for "pages build and deployment" workflow
3. Wait for the green checkmark ✓

### 6. Verify It's Live
Visit: https://fedres.github.io/Rivet/

## Troubleshooting

### If site doesn't load:

**Check 1: Verify the site files are in the right location**
```bash
# The site files should be in /site directory
ls site/
# Should show: index.html, styles.css, script.js, etc.
```

**Check 2: Ensure you're deploying from the production branch**
- Go to Settings > Pages
- Verify "production" branch is selected

**Check 3: Check if there's a custom domain configured**
- In Settings > Pages, look for "Custom domain"
- If set, make sure the domain is configured correctly
- Otherwise, leave it blank

**Check 4: Make sure the repository is public**
- Go to Settings > General
- Scroll to "Danger Zone"
- Verify repository visibility is "Public"

### If you see 404 error:

1. **Check the folder structure**
   - If deploying from `/site`, index.html must be at `/site/index.html`
   - If deploying from root, index.html must be at `/index.html`

2. **Our setup**: We have files in `/site/`, so:
   - Option 1: Deploy from `production` branch, `/site` folder
   - Option 2: Move files from `/site/` to root `/`

## Recommended Configuration

Since our files are in `/site/`, use:
- **Source**: Deploy from a branch  
- **Branch**: `production`
- **Folder**: `/site`

Then your site will be at: **https://fedres.github.io/Rivet/**

## Alternative: Move Files to Root

If you prefer to deploy from root:

```bash
# Move site files to root
mv site/* .
mv site/.* . 2>/dev/null || true
rmdir site/

# Commit
git add .
git commit -m "Move site to root for GitHub Pages"
git push origin production
```

Then configure:
- **Branch**: `production`
- **Folder**: `/ (root)`
