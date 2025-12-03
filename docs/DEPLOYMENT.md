# 🚀 GitHub Pages Deployment Guide

This guide will help you deploy your arcade neon RIVET website to GitHub Pages in just a few minutes!

## Quick Deployment (5 Minutes)

### Step 1: Create GitHub Repository

1. **Go to GitHub.com** and sign in to your account
2. **Click the "+" icon** in the top right corner
3. **Select "New repository"**
4. **Fill in the details:**
   - Repository name: `rivet-website` (or your preferred name)
   - Description: "Arcade neon website for RIVET - The C++ Dependency Revolution"
   - Set to **Public** (GitHub Pages requires public repos for free accounts)
   - ✅ Check "Add a README file"
5. **Click "Create repository"**

### Step 2: Upload Your Files

**Option A: Using GitHub Web Interface (Easiest)**

1. **Click "uploading an existing file"** link
2. **Drag and drop** all the website files:
   - `index.html`
   - `styles.css`
   - `script.js`
   - `404.html`
   - `_config.yml`
   - `README.md`
   - `.gitignore`

3. **Scroll down** and add commit message: "Initial website upload"
4. **Click "Commit changes"**

**Option B: Using Git Command Line**

```bash
# Clone your repository
git clone https://github.com/YOUR_USERNAME/rivet-website.git
cd rivet-website

# Copy your website files here
# (index.html, styles.css, script.js, etc.)

# Add and commit
git add .
git commit -m "Initial website upload"

# Push to GitHub
git push origin main
```

### Step 3: Enable GitHub Pages

1. **Go to your repository** on GitHub
2. **Click "Settings"** tab (top of repository)
3. **Scroll down** to "Pages" section in left sidebar
4. **Under "Source":**
   - Select **"Deploy from a branch"**
   - Branch: **"main"** (or "master")
   - Folder: **"/ (root)"**
5. **Click "Save"**

### Step 4: Access Your Website

🎉 **Your site will be live at:**
`https://YOUR_USERNAME.github.io/rivet-website`

GitHub will show a success message with your live URL!

---

## 🔧 Advanced Configuration

### Custom Domain Setup

If you have your own domain (e.g., `yourdomain.com`):

1. **Create CNAME file** in your repository root:
   ```
   yourdomain.com
   ```

2. **Add DNS records** with your domain provider:
   ```
   Type: CNAME
   Name: www
   Value: YOUR_USERNAME.github.io
   
   Type: A
   Name: @
   Value: 185.199.108.153
   Value: 185.199.109.153
   Value: 185.199.110.153
   Value: 185.199.111.153
   ```

3. **Wait 24-48 hours** for DNS propagation

### HTTPS Setup

GitHub Pages automatically provides HTTPS for:
- All custom domains
- `*.github.io` domains
- Site URLs that start with `https://`

### Branch Configuration

**Main branch (recommended):**
- Deploy from: `main` branch
- Folder: `/ (root)`
- Best for: Simple websites

**gh-pages branch:**
- Deploy from: `gh-pages` branch  
- Folder: `/ (root)`
- Best for: Project pages with separate documentation

### Jekyll Configuration (Optional)

Your `_config.yml` file is already configured for GitHub Pages:

```yaml
title: "RIVET: The C++ Dependency Revolution"
description: "High-performance C++ dependency management"
url: "https://YOUR_USERNAME.github.io/rivet-website"
baseurl: "/rivet-website"
```

---

## 🐛 Troubleshooting

### Site Not Loading

**Problem:** 404 error or site not found

**Solutions:**
1. ✅ Check repository is **Public**
2. ✅ Verify GitHub Pages is **enabled** in Settings > Pages
3. ✅ Confirm `index.html` is in **root directory**
4. ✅ Wait 5-10 minutes for deployment
5. ✅ Clear browser cache

### Build Failures

**Problem:** Jekyll build errors

**Solutions:**
1. Check `_config.yml` syntax
2. Verify all file paths are correct
3. Remove any unsupported plugins
4. Check GitHub Actions tab for detailed errors

### Custom Domain Not Working

**Problem:** Domain shows GitHub 404 page

**Solutions:**
1. ✅ Verify DNS records are correct
2. ✅ Check CNAME file is in root directory
3. ✅ Wait for DNS propagation (24-48 hours)
4. ✅ Contact domain provider if issues persist

### Slow Loading

**Problem:** Website loads slowly

**Solutions:**
1. Optimize images (use WebP format)
2. Minify CSS and JavaScript
3. Use GitHub's CDN (automatic)
4. Check network tab in browser DevTools

---

## 📊 Performance Tips

### Image Optimization
```bash
# Convert images to WebP for better compression
# Use tools like Squoosh or ImageOptim
```

### Code Minification
```bash
# CSS minification (optional)
npx clean-css-cli styles.css -o styles.min.css

# JavaScript minification (optional)  
npx terser script.js -o script.min.js
```

### Caching Headers
GitHub Pages automatically handles caching. For custom domains, add:
```
Cache-Control: public, max-age=31536000
```

---

## 🔄 Continuous Deployment

### Automatic Updates

Every time you push to your repository:
1. GitHub Pages automatically rebuilds your site
2. Changes are live within 1-2 minutes
3. No manual deployment needed!

### GitHub Actions (Advanced)

Create `.github/workflows/deploy.yml` for custom build process:

```yaml
name: Deploy to GitHub Pages

on:
  push:
    branches: [ main ]

jobs:
  deploy:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v2
    
    - name: Setup Ruby
      uses: ruby/setup-ruby@v1
      with:
        ruby-version: '3.0'
        
    - name: Install dependencies
      run: bundle install
      
    - name: Build site
      run: bundle exec jekyll build
      
    - name: Deploy to GitHub Pages
      uses: peaceiris/actions-gh-pages@v3
      with:
        github_token: ${{ secrets.GITHUB_TOKEN }}
        publish_dir: ./_site
```

---

## 📱 Mobile Optimization

Your website is already mobile-optimized with:
- ✅ Responsive grid layouts
- ✅ Touch-friendly navigation
- ✅ Optimized font sizes
- ✅ Fast loading on mobile networks

### Testing Mobile
1. Open website on mobile device
2. Use browser DevTools device simulation
3. Test on different screen sizes
4. Check loading speed with Lighthouse

---

## 🎨 Customization

### Change Colors
Edit CSS variables in `styles.css`:
```css
:root {
    --color-cyan: #00ffff;      /* Change to your preferred color */
    --color-magenta: #ff00ff;   /* Change to your preferred color */
    --color-purple: #9f00ff;    /* Change to your preferred color */
}
```

### Update Content
- **Hero section:** Edit `index.html` hero section
- **Features:** Modify features grid in HTML
- **Commands:** Update `script.js` command examples
- **Branding:** Change logo text and colors

### Add New Sections
1. Add HTML section in `index.html`
2. Style with CSS in `styles.css`
3. Add animations in `script.js`

---

## 🆘 Getting Help

### GitHub Pages Docs
- [Official GitHub Pages Documentation](https://docs.github.com/en/pages)
- [Jekyll on GitHub Pages](https://docs.github.com/en/pages/setting-up-a-github-pages-site-with-jekyll)

### Community Support
- [GitHub Community Forum](https://github.community/)
- [Stack Overflow GitHub Tag](https://stackoverflow.com/questions/tagged/github-pages)

### Report Issues
- Create issue in your repository
- Include browser console errors
- Provide steps to reproduce problems

---

**🎉 Congratulations! Your arcade neon RIVET website is now live on GitHub Pages!**

Need help? Check the troubleshooting section above or create an issue in your repository.