// ===== GLOBAL VARIABLES =====
const commands = {
    new: 'rivet new my_project --bin',
    build: 'rivet build --release',
    install: 'rivet install boost cmake',
    version: 'rivet --version'
};

const outputs = {
    new: [
        'Creating binary (application) `my_project` package',
        'Initialized git repository',
        'Added C++20 support',
        '✨ Ready in 0.012s'
    ],
    build: [
        'Compiling my_project (release)',
        'Linking executable',
        'Running tests...',
        '✅ Build successful in 2.34s'
    ],
    install: [
        'Resolving dependencies...',
        'Downloading boost-1.84.0',
        'Downloading cmake-3.27.0',
        'Verifying checksums...',
        '✨ Installation complete'
    ],
    version: [
        'rivet 0.1.0',
        'Rust 1.75.0',
        'C++20 standard',
        'Build: 2025-01-15'
    ]
};

// ===== DOM ELEMENTS =====
let currentCommand = 'new';
let isTyping = false;

// ===== UTILITY FUNCTIONS =====
function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

function copyToClipboard(text) {
    if (navigator.clipboard) {
        navigator.clipboard.writeText(text).then(() => {
            showCopyFeedback();
        }).catch(() => {
            fallbackCopyToClipboard(text);
        });
    } else {
        fallbackCopyToClipboard(text);
    }
}

function fallbackCopyToClipboard(text) {
    const textArea = document.createElement('textarea');
    textArea.value = text;
    document.body.appendChild(textArea);
    textArea.select();
    document.execCommand('copy');
    document.body.removeChild(textArea);
    showCopyFeedback();
}

function showCopyFeedback() {
    const button = document.querySelector('.copy-button');
    const originalHTML = button.innerHTML;
    button.innerHTML = '✓';
    button.style.background = 'var(--color-success)';
    button.style.color = 'var(--color-background)';
    
    setTimeout(() => {
        button.innerHTML = originalHTML;
        button.style.background = '';
        button.style.color = '';
    }, 2000);
}

async function typewriterEffect(element, text, speed = 50) {
    if (isTyping) return;
    isTyping = true;
    
    element.textContent = '';
    for (let i = 0; i < text.length; i++) {
        element.textContent += text[i];
        await sleep(speed);
    }
    
    isTyping = false;
}

async function typeLines(element, lines, delay = 100) {
    element.innerHTML = '';
    for (const line of lines) {
        const lineElement = document.createElement('div');
        lineElement.className = 'output-line';
        element.appendChild(lineElement);
        
        await typewriterEffect(lineElement, line, 30);
        await sleep(delay);
    }
}

// ===== TERMINAL FUNCTIONALITY =====
async function copyInstallCommand() {
    const command = 'bash <(curl -sSL https://rivet.sh/install.sh)';
    copyToClipboard(command);
}

async function initializeTerminal() {
    const terminal = document.querySelector('.install-terminal');
    const commandElement = document.getElementById('install-command');
    const outputElement = document.getElementById('terminal-output');
    
    // Simulate installation process
    await typewriterEffect(commandElement, 'bash <(curl -sSL https://rivet.sh/install.sh)', 100);
    await sleep(500);
    
    const installLines = [
        'Detecting operating system... Linux x86_64',
        'Downloading Rivet v0.1.0...',
        'Verifying GPG signature...',
        'Installing to ~/.rivet/bin...',
        'Adding to PATH...',
        '✨ Rivet installed successfully!'
    ];
    
    await typeLines(outputElement, installLines, 200);
}

// ===== COMMAND INTERFACE =====
function switchCommand(command) {
    if (isTyping || command === currentCommand) return;
    
    currentCommand = command;
    const commandElement = document.getElementById('current-command');
    const outputElement = document.getElementById('command-output');
    const tabButtons = document.querySelectorAll('.command-tab');
    
    // Update active tab
    tabButtons.forEach(btn => {
        btn.classList.remove('active');
        if (btn.dataset.command === command) {
            btn.classList.add('active');
        }
    });
    
    // Update command and output
    typewriterEffect(commandElement, commands[command], 80).then(() => {
        setTimeout(() => {
            typeLines(outputElement, outputs[command], 150);
        }, 300);
    });
}

function initializeCommandInterface() {
    const tabButtons = document.querySelectorAll('.command-tab');
    
    tabButtons.forEach(button => {
        button.addEventListener('click', () => {
            switchCommand(button.dataset.command);
        });
    });
    
    // Initialize with default command
    setTimeout(() => {
        typeLines(document.getElementById('command-output'), outputs[currentCommand], 200);
    }, 1000);
}

// ===== SCROLL ANIMATIONS =====
function initializeScrollAnimations() {
    const observerOptions = {
        threshold: 0.1,
        rootMargin: '0px 0px -50px 0px'
    };
    
    const observer = new IntersectionObserver((entries) => {
        entries.forEach(entry => {
            if (entry.isIntersecting) {
                entry.target.style.opacity = '1';
                entry.target.style.transform = 'translateY(0)';
                
                // Add staggered animation for feature cards
                if (entry.target.classList.contains('features-grid')) {
                    const cards = entry.target.querySelectorAll('.feature-card');
                    cards.forEach((card, index) => {
                        setTimeout(() => {
                            card.style.opacity = '1';
                            card.style.transform = 'translateY(0)';
                        }, index * 100);
                    });
                }
                
                // Animate timeline items
                if (entry.target.classList.contains('roadmap-timeline')) {
                    const items = entry.target.querySelectorAll('.timeline-item');
                    items.forEach((item, index) => {
                        setTimeout(() => {
                            item.style.opacity = '1';
                            item.style.transform = 'translateX(0)';
                        }, index * 200);
                    });
                }
            }
        });
    }, observerOptions);
    
    // Observe elements for animation
    const elementsToAnimate = document.querySelectorAll(`
        .features-grid,
        .architecture-diagram,
        .command-interface,
        .flow-steps,
        .roadmap-timeline,
        .timeline-item
    `);
    
    elementsToAnimate.forEach(el => {
        el.style.opacity = '0';
        el.style.transform = 'translateY(30px)';
        el.style.transition = 'opacity 0.6s ease, transform 0.6s ease';
        observer.observe(el);
    });
    
    // Special handling for individual feature cards
    document.querySelectorAll('.feature-card').forEach((card, index) => {
        card.style.opacity = '0';
        card.style.transform = 'translateY(30px)';
        card.style.transition = `opacity 0.6s ease ${index * 0.1}s, transform 0.6s ease ${index * 0.1}s`;
    });
    
    // Special handling for timeline items
    document.querySelectorAll('.timeline-item').forEach((item, index) => {
        item.style.opacity = '0';
        item.style.transform = 'translateX(-30px)';
        item.style.transition = `opacity 0.6s ease ${index * 0.2}s, transform 0.6s ease ${index * 0.2}s`;
    });
}

// ===== INTERACTIVE EFFECTS =====
function initializeInteractiveEffects() {
    // Add glow effect to feature cards on hover
    const featureCards = document.querySelectorAll('.feature-card');
    featureCards.forEach(card => {
        card.addEventListener('mouseenter', () => {
            const icon = card.querySelector('.feature-icon');
            if (icon) {
                icon.style.filter = 'drop-shadow(0 0 8px var(--color-magenta))';
            }
        });
        
        card.addEventListener('mouseleave', () => {
            const icon = card.querySelector('.feature-icon');
            if (icon) {
                icon.style.filter = 'drop-shadow(0 0 4px var(--color-magenta))';
            }
        });
    });
    
    // Add pulse effect to architecture connector
    const connector = document.querySelector('.data-flow');
    if (connector) {
        setInterval(() => {
            connector.style.opacity = '0.3';
            setTimeout(() => {
                connector.style.opacity = '1';
            }, 200);
        }, 3000);
    }
    
    // Add floating animation to stats
    const stats = document.querySelectorAll('.stat');
    stats.forEach((stat, index) => {
        stat.style.animation = `float 3s ease-in-out ${index * 0.5}s infinite`;
    });
}

// ===== GLITCH EFFECTS =====
function initializeGlitchEffects() {
    const glitchElements = document.querySelectorAll('.glitch');
    
    glitchElements.forEach(element => {
        // Random glitch trigger
        setInterval(() => {
            if (Math.random() < 0.1) { // 10% chance every interval
                element.style.animation = 'none';
                setTimeout(() => {
                    element.style.animation = '';
                }, 100);
            }
        }, 2000);
    });
}

// ===== SMOOTH SCROLLING =====
function initializeSmoothScrolling() {
    const navLinks = document.querySelectorAll('.nav-link[href^="#"]');
    
    navLinks.forEach(link => {
        link.addEventListener('click', (e) => {
            e.preventDefault();
            const targetId = link.getAttribute('href').substring(1);
            const targetElement = document.getElementById(targetId);
            
            if (targetElement) {
                const navHeight = document.querySelector('.nav').offsetHeight;
                const targetPosition = targetElement.offsetTop - navHeight - 20;
                
                window.scrollTo({
                    top: targetPosition,
                    behavior: 'smooth'
                });
            }
        });
    });
}

// ===== CURSOR FOLLOWING =====
function initializeCursorEffects() {
    let mouseX = 0;
    let mouseY = 0;
    
    document.addEventListener('mousemove', (e) => {
        mouseX = e.clientX;
        mouseY = e.clientY;
    });
    
    // Create trailing cursor effect
    const cursor = document.createElement('div');
    cursor.className = 'cursor-trail';
    cursor.style.cssText = `
        position: fixed;
        width: 20px;
        height: 20px;
        background: radial-gradient(circle, var(--color-cyan), transparent);
        border-radius: 50%;
        pointer-events: none;
        z-index: 9999;
        opacity: 0;
        transition: opacity 0.3s ease;
    `;
    document.body.appendChild(cursor);
    
    document.addEventListener('mouseenter', () => {
        cursor.style.opacity = '0.6';
    });
    
    document.addEventListener('mouseleave', () => {
        cursor.style.opacity = '0';
    });
    
    function animateCursor() {
        cursor.style.left = mouseX - 10 + 'px';
        cursor.style.top = mouseY - 10 + 'px';
        requestAnimationFrame(animateCursor);
    }
    
    animateCursor();
}

// ===== PERFORMANCE MONITORING =====
function initializePerformanceMonitoring() {
    // Monitor scroll performance
    let scrollTimeout;
    window.addEventListener('scroll', () => {
        if (scrollTimeout) {
            clearTimeout(scrollTimeout);
        }
        
        scrollTimeout = setTimeout(() => {
            // Reduce animations on slow devices
            if (window.innerWidth < 768) {
                document.body.classList.add('mobile');
            }
        }, 150);
    });
    
    // Monitor for reduced motion preference
    if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) {
        document.body.classList.add('reduce-motion');
    }
}

// ===== ANIMATION KEYFRAMES =====
function addCustomKeyframes() {
    const style = document.createElement('style');
    style.textContent = `
        @keyframes float {
            0%, 100% { transform: translateY(0px); }
            50% { transform: translateY(-10px); }
        }
        
        @keyframes pulse-glow {
            0%, 100% { 
                box-shadow: 0 0 12px rgba(159, 0, 255, 0.4), 
                           inset 0 0 8px rgba(20, 20, 43, 0.9);
            }
            50% { 
                box-shadow: 0 0 20px rgba(0, 255, 255, 0.6), 
                           inset 0 0 8px rgba(20, 20, 43, 0.7);
            }
        }
        
        .feature-card:hover {
            animation: pulse-glow 2s ease-in-out infinite;
        }
        
        .mobile .glitch::before,
        .mobile .glitch::after {
            animation: none;
        }
        
        .reduce-motion * {
            animation-duration: 0.01ms !important;
            animation-iteration-count: 1 !important;
            transition-duration: 0.01ms !important;
        }
    `;
    document.head.appendChild(style);
}

// ===== KEYBOARD NAVIGATION =====
function initializeKeyboardNavigation() {
    document.addEventListener('keydown', (e) => {
        // Command tab switching with arrow keys
        if (e.target.classList.contains('command-tab')) {
            const tabs = Array.from(document.querySelectorAll('.command-tab'));
            const currentIndex = tabs.indexOf(e.target);
            
            if (e.key === 'ArrowRight' && currentIndex < tabs.length - 1) {
                e.preventDefault();
                tabs[currentIndex + 1].focus();
                tabs[currentIndex + 1].click();
            } else if (e.key === 'ArrowLeft' && currentIndex > 0) {
                e.preventDefault();
                tabs[currentIndex - 1].focus();
                tabs[currentIndex - 1].click();
            }
        }
        
        // Copy install command with Ctrl+C when focused
        if ((e.ctrlKey || e.metaKey) && e.key === 'c' && 
            document.activeElement.closest('.install-terminal')) {
            e.preventDefault();
            copyInstallCommand();
        }
    });
}

// ===== ERROR HANDLING =====
function initializeErrorHandling() {
    window.addEventListener('error', (e) => {
        console.error('JavaScript Error:', e.error);
        // Could send to analytics service in production
    });
    
    // Handle unhandled promise rejections
    window.addEventListener('unhandledrejection', (e) => {
        console.error('Unhandled Promise Rejection:', e.reason);
        e.preventDefault();
    });
}

// ===== ACCESSIBILITY ENHANCEMENTS =====
function initializeAccessibility() {
    // Add skip link
    const skipLink = document.createElement('a');
    skipLink.href = '#main-content';
    skipLink.textContent = 'Skip to main content';
    skipLink.className = 'skip-link';
    skipLink.style.cssText = `
        position: absolute;
        top: -40px;
        left: 6px;
        background: var(--color-cyan);
        color: var(--color-background);
        padding: 8px;
        text-decoration: none;
        z-index: 10000;
        transition: top 0.3s ease;
    `;
    skipLink.addEventListener('focus', () => {
        skipLink.style.top = '6px';
    });
    skipLink.addEventListener('blur', () => {
        skipLink.style.top = '-40px';
    });
    document.body.insertBefore(skipLink, document.body.firstChild);
    
    // Add main content landmark
    const heroSection = document.querySelector('.hero');
    if (heroSection) {
        heroSection.id = 'main-content';
        heroSection.setAttribute('role', 'main');
    }
    
    // Improve focus management
    const focusableElements = document.querySelectorAll(
        'a[href], button, textarea, input[type="text"], input[type="radio"], input[type="checkbox"], select'
    );
    
    focusableElements.forEach(element => {
        element.addEventListener('focus', () => {
            element.style.outline = '2px solid var(--color-cyan)';
            element.style.outlineOffset = '2px';
        });
        
        element.addEventListener('blur', () => {
            element.style.outline = '';
            element.style.outlineOffset = '';
        });
    });
}

// ===== MAIN INITIALIZATION =====
document.addEventListener('DOMContentLoaded', async () => {
    try {
        // Initialize all modules
        addCustomKeyframes();
        initializeSmoothScrolling();
        initializeKeyboardNavigation();
        initializeErrorHandling();
        initializeAccessibility();
        initializePerformanceMonitoring();
        initializeScrollAnimations();
        initializeInteractiveEffects();
        initializeGlitchEffects();
        
        // Initialize terminal functionality
        await initializeTerminal();
        initializeCommandInterface();
        
        // Optional: Cursor effects (commented out as it might be distracting)
        // initializeCursorEffects();
        
        console.log('🚀 Rivet website initialized successfully!');
        
    } catch (error) {
        console.error('❌ Failed to initialize website:', error);
    }
});

// ===== EXPORT FUNCTIONS FOR GLOBAL ACCESS =====
window.copyInstallCommand = copyInstallCommand;
window.switchCommand = switchCommand;