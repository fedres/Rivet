// Terminal Animation
document.addEventListener('DOMContentLoaded', function () {
    animateInstallTerminal();
    animateDemoTerminal();
    setupCodeCopy();
});

// Install Terminal Animation
function animateInstallTerminal() {
    const outputDiv = document.getElementById('terminal-output');
    if (!outputDiv) return;

    const steps = [
        { output: 'Detecting operating system... macOS (ARM64)', delay: 500, color: '#888' },
        { output: 'Downloading Rivet v0.1.0...', delay: 1200, color: '#888' },
        { output: 'Verifying GPG signature...', delay: 1800, color: '#888' },
        { output: 'Installing to ~/.rivet/bin...', delay: 2400, color: '#888' },
        { output: 'Adding to PATH...', delay: 2800, color: '#888' },
        { success: '✨ Rivet installed successfully!', delay: 3200 }
    ];

    let currentStep = 0;

    function typeStep() {
        if (currentStep >= steps.length) return;

        const step = steps[currentStep];
        setTimeout(() => {
            const line = document.createElement('div');
            line.className = step.success ? 'output-line success' : 'output-line';
            if (step.color) line.style.color = step.color;
            line.textContent = step.output || step.success;
            outputDiv.appendChild(line);

            outputDiv.scrollTop = outputDiv.scrollHeight;
            currentStep++;
            typeStep();
        }, step.delay - (currentStep > 0 ? steps[currentStep - 1].delay : 0));
    }

    typeStep();
}

// Live Build Demo Animation
function animateDemoTerminal() {
    const outputDiv = document.getElementById('demo-output');
    if (!outputDiv) return;

    const steps = [
        { output: '📦 Discovering workspace members...', delay: 500, color: '#00ffff' },
        { output: '  → mathlib', delay: 800 },
        { output: '  → stringutils', delay: 1000 },
        { output: '  → app', delay: 1200 },
        { output: '🔨 Building workspace with 3 packages', delay: 1500, color: '#00ffff' },

        { output: '\nBuilding mathlib', delay: 2000, color: '#ff00ff' },
        { output: '  Compiling library mathlib', delay: 2200 },
        { output: '   Compiling ./mathlib/src/lib.cpp', delay: 2500, color: '#888' },

        { output: '\nBuilding stringutils', delay: 3500, color: '#ff00ff' },
        { output: '  Compiling library stringutils', delay: 3700 },
        { output: '   Compiling ./stringutils/src/lib.cpp', delay: 4000, color: '#888' },

        { output: '\nBuilding app', delay: 5000, color: '#ff00ff' },
        { output: '  Compiling binary calculator', delay: 5200 },
        { output: '    Finished ./target/debug/calculator', delay: 6000, color: '#00ff88' },

        { success: '\n✓ Build complete', delay: 6500 }
    ];

    let currentStep = 0;

    function runAnimation() {
        outputDiv.innerHTML = '';
        currentStep = 0;

        function typeStep() {
            if (currentStep >= steps.length) {
                setTimeout(runAnimation, 5000); // Loop after 5 seconds
                return;
            }

            const step = steps[currentStep];
            setTimeout(() => {
                const line = document.createElement('div');
                line.className = step.success ? 'output-line success' : 'output-line';
                if (step.color) line.style.color = step.color;
                line.textContent = step.output || step.success;
                outputDiv.appendChild(line);

                outputDiv.scrollTop = outputDiv.scrollHeight;
                currentStep++;
                typeStep();
            }, step.delay - (currentStep > 0 ? steps[currentStep - 1].delay : 0));
        }

        typeStep();
    }

    runAnimation();
}

function setupCodeCopy() {
    document.querySelectorAll('.code-block').forEach(block => {
        block.addEventListener('click', function () {
            const code = this.textContent;
            navigator.clipboard.writeText(code).then(() => {
                const copied = document.createElement('div');
                copied.className = 'copied-notification';
                copied.textContent = '✓ Copied!';
                this.appendChild(copied);
                setTimeout(() => copied.remove(), 2000);
            });
        });
    });
}

function copyInstallCommand() {
    const command = 'bash <(curl -sSL https://rivet.sh/install.sh)';
    navigator.clipboard.writeText(command).then(() => {
        const button = document.querySelector('.copy-button');
        const original = button.innerHTML;
        button.innerHTML = '✓';
        setTimeout(() => button.innerHTML = original, 2000);
    });
}

// Smooth scroll for navigation links
document.querySelectorAll('a[href^="#"]').forEach(anchor => {
    anchor.addEventListener('click', function (e) {
        e.preventDefault();
        const target = document.querySelector(this.getAttribute('href'));
        if (target) {
            target.scrollIntoView({ behavior: 'smooth', block: 'start' });
        }
    });
});

// Add intersection observer for fade-in animations
const observerOptions = {
    threshold: 0.1,
    rootMargin: '0px 0px -100px 0px'
};

const observer = new IntersectionObserver((entries) => {
    entries.forEach(entry => {
        if (entry.isIntersecting) {
            entry.target.style.opacity = '1';
            entry.target.style.transform = 'translateY(0)';
        }
    });
}, observerOptions);

document.querySelectorAll('.feature-card, .mini-terminal, .vim-editor, .platform-card').forEach(el => {
    el.style.opacity = '0';
    el.style.transform = 'translateY(20px)';
    el.style.transition = 'opacity 0.6s ease, transform 0.6s ease';
    observer.observe(el);
});