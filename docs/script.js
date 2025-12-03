// Terminal Animation
document.addEventListener('DOMContentLoaded', function () {
    animateInstallTerminal();
    setupCommandTabs();
    setupCodeCopy();
});

function animateInstallTerminal() {
    const installCommand = document.getElementById('install-command');
    if (!installCommand) return;

    const steps = [
        { text: '$ git clone https://github.com/fedres/Rivet.git', delay: 0 },
        { output: 'Cloning into \'Rivet\'...', delay: 800, color: '#888' },
        { output: 'remote: Enumerating objects: 1234, done.', delay: 1200, color: '#888' },
        { output: 'remote: Counting objects: 100%', delay: 1500, color: '#888' },
        { output: 'Receiving objects: 100% (1234/1234), 2.5 MiB | 5.0 MiB/s, done.', delay: 1800, color: '#888' },
        { text: '$ cd Rivet/rivet', delay: 2200 },
        { text: '$ cargo build --release', delay: 2600 },
        { output: '   Compiling rivet-cli v0.1.0', delay: 3000, color: '#0f0' },
        { output: '    Finished release [optimized] target(s) in 12.3s', delay: 4500, color: '#0f0' },
        { text: '$ rivet --version', delay: 5000 },
        { output: 'rivet 0.1.0', delay: 5300, color: '#0ff' },
        { text: '$ rivet new my_project', delay: 5800 },
        { output: '     Created binary (application) `my_project` package', delay: 6200, color: '#0f0' },
        { text: '$ cd my_project && rivet build', delay: 6800 },
        { output: '   Compiling my_project v0.1.0', delay: 7200, color: '#0f0' },
        { output: '    Finished `dev` profile [unoptimized + debuginfo] target(s) in 0.5s', delay: 8000, color: '#0f0' },
        { text: '$ rivet run', delay: 8500 },
        { output: '     Running `target/debug/my_project`', delay: 8900, color: '#888' },
        { output: 'Hello from Rivet!', delay: 9200, color: '#ff0' },
        { success: '✨ Ready to build amazing C++ projects!', delay: 9800 }
    ];

    const outputDiv = installCommand.closest('.terminal-content').querySelector('.terminal-output') ||
        installCommand.closest('.install-terminal').querySelector('.terminal-output');

    if (!outputDiv) return;

    let currentStep = 0;

    function typeStep() {
        if (currentStep >= steps.length) {
            setTimeout(() => {
                currentStep = 0;
                outputDiv.innerHTML = '';
                typeStep();
            }, 5000);
            return;
        }

        const step = steps[currentStep];

        setTimeout(() => {
            if (step.text) {
                const line = document.createElement('div');
                line.className = 'terminal-line';
                line.innerHTML = `<span class="prompt">$</span> <span class="command">${step.text}</span>`;
                outputDiv.appendChild(line);
            } else if (step.output) {
                const line = document.createElement('div');
                line.className = 'output-line';
                line.style.color = step.color || '#888';
                line.textContent = step.output;
                outputDiv.appendChild(line);
            } else if (step.success) {
                const line = document.createElement('div');
                line.className = 'output-line success';
                line.textContent = step.success;
                outputDiv.appendChild(line);
            }

            outputDiv.scrollTop = outputDiv.scrollHeight;
            currentStep++;
            typeStep();
        }, step.delay);
    }

    typeStep();
}

function setupCommandTabs() {
    const tabs = document.querySelectorAll('.command-tab');
    const commands = {
        'new': {
            command: 'rivet new my_project',
            output: [
                '     Created binary (application) `my_project` package',
                '✨ Ready in 0.012s'
            ]
        },
        'build': {
            command: 'rivet build',
            output: [
                '   Compiling my_project v0.1.0',
                '    Finished `dev` profile [unoptimized + debuginfo] target(s) in 0.8s'
            ]
        },
        'install': {
            command: 'rivet add boost',
            output: [
                '📦 Installing 1 dependencies...',
                '   Installing boost...',
                '✓ Dependencies installed successfully'
            ]
        },
        'version': {
            command: 'rivet --version',
            output: [
                'rivet 0.1.0'
            ]
        }
    };

    tabs.forEach(tab => {
        tab.addEventListener('click', function () {
            tabs.forEach(t => t.classList.remove('active'));
            this.classList.add('active');

            const cmd = this.dataset.command;
            const data = commands[cmd];

            const commandEl = document.getElementById('current-command');
            const outputEl = document.getElementById('command-output');

            if (commandEl && outputEl && data) {
                commandEl.textContent = data.command;
                outputEl.innerHTML = data.output.map(line => {
                    const className = line.includes('✓') || line.includes('✨') ? 'output-line success' : 'output-line';
                    return `<div class="${className}">${line}</div>`;
                }).join('');
            }
        });
    });
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
    const command = 'git clone https://github.com/fedres/Rivet.git && cd Rivet/rivet && cargo build --release';
    navigator.clipboard.writeText(command).then(() => {
        const button = event.target.closest('.copy-button');
        const original = button.innerHTML;
        button.innerHTML = '✓ Copied!';
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

document.querySelectorAll('.feature-card, .command-group, .example-group, .platform-card').forEach(el => {
    el.style.opacity = '0';
    el.style.transform = 'translateY(20px)';
    el.style.transition = 'opacity 0.6s ease, transform 0.6s ease';
    observer.observe(el);
});