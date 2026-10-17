/* Tamias 官网 / 文档站的小交互（渐进增强：没有 JS 也能正常读文档）。 */
(function () {
  'use strict';

  function ready(fn) {
    if (document.readyState === 'loading') {
      document.addEventListener('DOMContentLoaded', fn);
    } else {
      fn();
    }
  }

  ready(function () {
    var body = document.body;
    var header = document.getElementById('tm-header');
    var isHome = body.classList.contains('homepage');
    window.__tmSiteReady = true;

    /* 首页顶部：暗色 hero 上让顶栏透明，滚下去变实心玻璃。
       is-over / is-solid 两个状态分开，方便 CSS 各自写过渡。 */
    if (header && isHome) {
      var syncHeader = function () {
        var y = window.scrollY || window.pageYOffset || 0;
        header.classList.toggle('is-over', y < 40);
        header.classList.toggle('is-solid', y >= 40);
      };
      syncHeader();
      window.addEventListener('scroll', syncHeader, { passive: true });
    }

    /* 顶栏汉堡菜单（小屏） */
    var navToggle = document.querySelector('[data-tm-toggle="nav"]');
    var nav = document.getElementById('tm-nav');
    if (navToggle && nav) {
      navToggle.addEventListener('click', function () {
        var open = nav.classList.toggle('is-open');
        navToggle.setAttribute('aria-expanded', open ? 'true' : 'false');
      });
    }

    /* 文档目录折叠（小屏） */
    var treeToggle = document.querySelector('[data-tm-toggle="tree"]');
    var tree = document.getElementById('tm-doc-tree');
    if (treeToggle && tree) {
      treeToggle.addEventListener('click', function () {
        var open = tree.classList.toggle('is-open');
        treeToggle.setAttribute('aria-expanded', open ? 'true' : 'false');
      });
    }

    /* 宽表格加横向滚动容器，窄屏不撑破版心 */
    var tables = document.querySelectorAll('.tm-doc-body table');
    for (var i = 0; i < tables.length; i++) {
      var table = tables[i];
      if (table.parentNode && table.parentNode.classList.contains('tm-table-wrap')) {
        continue;
      }
      var wrap = document.createElement('div');
      wrap.className = 'tm-table-wrap';
      table.parentNode.insertBefore(wrap, table);
      wrap.appendChild(table);
    }

    /* 图片灯箱：带 data-tm-lightbox 的元素点一下全屏看大图。
       关闭方式：右上角按钮 / 点背景 / Esc。 */
    var lightboxTriggers = document.querySelectorAll('[data-tm-lightbox]');
    if (lightboxTriggers.length) {
      var lightbox = document.createElement('div');
      lightbox.className = 'tm-lightbox';
      lightbox.setAttribute('role', 'dialog');
      lightbox.setAttribute('aria-modal', 'true');
      lightbox.setAttribute('aria-label', '图片全屏查看');
      lightbox.setAttribute('hidden', 'hidden');
      lightbox.innerHTML =
        '<button type="button" class="tm-lightbox-close">' +
        '<i class="fas fa-times" aria-hidden="true"></i> 关闭' +
        '</button>' +
        '<img class="tm-lightbox-img" alt="">' +
        '<p class="tm-lightbox-tip">按 Esc 或点击空白处关闭</p>';
      document.body.appendChild(lightbox);

      var lightboxImg = lightbox.querySelector('.tm-lightbox-img');
      var lightboxClose = lightbox.querySelector('.tm-lightbox-close');
      var lightboxSource = null;
      lightboxClose.setAttribute('aria-label', '关闭全屏查看');

      var openLightbox = function (trigger) {
        var inner = trigger.querySelector('img');
        var src = trigger.getAttribute('data-tm-lightbox') || (inner ? inner.src : '');
        if (!src) {
          return;
        }
        lightboxImg.setAttribute('src', src);
        lightboxImg.setAttribute('alt', inner ? inner.getAttribute('alt') || '' : '');
        lightboxSource = trigger;
        lightbox.removeAttribute('hidden');
        document.documentElement.classList.add('tm-lightbox-open');
        lightboxClose.focus();
      };

      var closeLightbox = function () {
        if (lightbox.hasAttribute('hidden')) {
          return;
        }
        lightbox.setAttribute('hidden', 'hidden');
        lightboxImg.removeAttribute('src');
        document.documentElement.classList.remove('tm-lightbox-open');
        if (lightboxSource && lightboxSource.focus) {
          lightboxSource.focus();
        }
        lightboxSource = null;
      };

      var bindTrigger = function (trigger) {
        trigger.addEventListener('click', function () {
          openLightbox(trigger);
        });
      };
      for (var t = 0; t < lightboxTriggers.length; t++) {
        bindTrigger(lightboxTriggers[t]);
      }

      lightboxClose.addEventListener('click', closeLightbox);
      lightboxImg.addEventListener('click', function (event) {
        event.stopPropagation();
      });
      lightbox.addEventListener('click', closeLightbox);
      document.addEventListener('keydown', function (event) {
        var key = event.key || '';
        if (key === 'Escape' || key === 'Esc' || event.keyCode === 27) {
          closeLightbox();
        }
      });
    }

    /* ---------------------------------------------------------------
       首页专用的小交互。都只在首页元素存在时生效，文档页不受影响。
       --------------------------------------------------------------- */
    if (isHome) {
      var reduceMotion = window.matchMedia &&
        window.matchMedia('(prefers-reduced-motion: reduce)').matches;

      /* 0. 阅读进度、回到顶部、顶栏当前小节高亮。 */
      var progressBar = document.querySelector('.tm-progress i');
      var topButton = document.querySelector('.tm-top');
      var navSectionLinks = [];
      var navLinks = document.querySelectorAll('.tm-nav .tm-nav-link');
      for (var ni = 0; ni < navLinks.length; ni++) {
        var hash = navLinks[ni].hash;
        if (!hash || hash === '#') continue;
        var section = document.querySelector(hash);
        if (section) navSectionLinks.push({ link: navLinks[ni], section: section });
      }

      var syncScrollUi = function () {
        var y = window.scrollY || window.pageYOffset || 0;
        var max = Math.max(1, document.documentElement.scrollHeight - window.innerHeight);
        if (progressBar) {
          progressBar.style.transform =
            'scaleX(' + Math.min(1, Math.max(0, y / max)).toFixed(4) + ')';
        }
        // 产品截图区域是点击看大图的热区，回到顶部先让开，等它滚出视口再出现。
        var product = document.getElementById('app');
        var productGone = product ? product.getBoundingClientRect().bottom < 0 : true;
        if (topButton) {
          topButton.classList.toggle('is-visible', productGone && y > 720);
        }

        var active = -1;
        for (var i = 0; i < navSectionLinks.length; i++) {
          var rect = navSectionLinks[i].section.getBoundingClientRect();
          if (rect.top <= 170 && rect.bottom > 170) active = i;
        }
        for (var j = 0; j < navSectionLinks.length; j++) {
          var on = j === active;
          navSectionLinks[j].link.classList.toggle('is-active', on);
          if (on) navSectionLinks[j].link.setAttribute('aria-current', 'location');
          else navSectionLinks[j].link.removeAttribute('aria-current');
        }
      };

      syncScrollUi();
      window.addEventListener('scroll', syncScrollUi, { passive: true });
      window.addEventListener('resize', syncScrollUi);

      if (topButton) {
        topButton.addEventListener('click', function () {
          window.scrollTo({ top: 0, behavior: reduceMotion ? 'auto' : 'smooth' });
        });
      }

      /* 0.5 无限跑马灯：一组先填到至少一屏宽，再原样复制第二组。
         动画只走 -50%，接缝正好落在同一组内容上，右侧不会断开。 */
      var marquee = document.querySelector('.tm-marquee');
      var marqueeTrack = marquee && marquee.querySelector('.tm-marquee-track');
      var marqueeGroup = marqueeTrack && marqueeTrack.querySelector('.tm-marquee-group');
      if (marquee && marqueeTrack && marqueeGroup) {
        var marqueeBase = marqueeGroup.cloneNode(true);
        var buildMarquee = function () {
          marquee.classList.remove('is-ready');
          marqueeTrack.innerHTML = '';
          var group = marqueeBase.cloneNode(true);
          marqueeTrack.appendChild(group);
          var items = Array.prototype.slice.call(group.children);
          var minWidth = Math.max(marquee.clientWidth + 80, 900);
          var guard = 0;
          while (group.getBoundingClientRect().width < minWidth && guard < 12) {
            for (var i = 0; i < items.length; i++) {
              group.appendChild(items[i].cloneNode(true));
            }
            guard += 1;
          }
          var clone = group.cloneNode(true);
          clone.setAttribute('aria-hidden', 'true');
          marqueeTrack.appendChild(clone);
          var groupWidth = group.getBoundingClientRect().width;
          var duration = Math.max(30, Math.round(groupWidth / 58));
          marquee.style.setProperty('--marquee-duration', duration + 's');
          // 强制重排后再启动，保证每次都从 0 开始
          void marqueeTrack.offsetWidth;
          marquee.classList.add('is-ready');
        };
        buildMarquee();
        var marqueeResizeTimer = 0;
        window.addEventListener('resize', function () {
          window.clearTimeout(marqueeResizeTimer);
          marqueeResizeTimer = window.setTimeout(buildMarquee, 180);
        });
      }

      /* 1. 滚动显现：section 标题、流程、卡片依次淡入。 */
      var revealNodes = document.querySelectorAll(
        '[data-reveal], .tm-bento-card, .tm-doc-card, .tm-note, .tm-flow-step, .tm-mini-card'
      );
      var revealAll = function () {
        for (var i = 0; i < revealNodes.length; i++) {
          revealNodes[i].classList.add('is-visible');
          revealNodes[i].style.transitionDelay = '';
        }
      };

      if (reduceMotion || !('IntersectionObserver' in window)) {
        revealAll();
      } else {
        var revealObserver = new IntersectionObserver(
          function (entries) {
            for (var i = 0; i < entries.length; i++) {
              if (!entries[i].isIntersecting) continue;
              var el = entries[i].target;
              var index = 0;
              var prev = el.previousElementSibling;
              while (prev) {
                index += 1;
                prev = prev.previousElementSibling;
              }
              // 同组最多错开 5 个，延迟只用于入场，动画结束后清掉，
              // 免得影响卡片自己的 hover 过渡。
              var delay = Math.min(index, 5) * 55;
              el.style.transitionDelay = delay + 'ms';
              el.classList.add('is-visible');
              setTimeout(function (node) {
                node.style.transitionDelay = '';
              }, 900 + delay, el);
              revealObserver.unobserve(el);
            }
          },
          { rootMargin: '0px 0px -6% 0px', threshold: 0.06 }
        );
        for (var r = 0; r < revealNodes.length; r++) {
          revealObserver.observe(revealNodes[r]);
        }
      }

      /* 2. Bento 卡片：跟随指针的柔光。 */
      var glowCards = document.querySelectorAll('[data-glow]');
      for (var g = 0; g < glowCards.length; g++) {
        (function (card) {
          card.addEventListener('pointermove', function (event) {
            var rect = card.getBoundingClientRect();
            card.style.setProperty('--mx', ((event.clientX - rect.left) / rect.width * 100) + '%');
            card.style.setProperty('--my', ((event.clientY - rect.top) / rect.height * 100) + '%');
          });
        })(glowCards[g]);
      }

      /* 3. 架构：列表 ↔ 层板联动。层板本身 aria-hidden，键盘走列表。 */
      var archItems = document.querySelectorAll('[data-layer-link]');
      var archLayers = document.querySelectorAll('[data-layer]');
      var activeLayer = -1;

      var setActiveLayer = function (index) {
        activeLayer = index;
        for (var i = 0; i < archLayers.length; i++) {
          var on = Number(archLayers[i].getAttribute('data-layer')) === index;
          archLayers[i].classList.toggle('is-active', on);
          archLayers[i].classList.toggle('is-dim', index >= 0 && !on);
        }
        for (var j = 0; j < archItems.length; j++) {
          archItems[j].classList.toggle(
            'is-active',
            Number(archItems[j].getAttribute('data-layer-link')) === index
          );
        }
      };

      var clearActiveLayer = function () {
        // 回到顶层而不是全灭：默认状态也有一条被点亮的边，画面不会突然变空。
        setActiveLayer(0);
      };

      for (var ai = 0; ai < archItems.length; ai++) {
        (function (item) {
          var index = Number(item.getAttribute('data-layer-link'));
          item.addEventListener('mouseenter', function () { setActiveLayer(index); });
          item.addEventListener('focus', function () { setActiveLayer(index); });
          item.addEventListener('mouseleave', clearActiveLayer);
          item.addEventListener('blur', clearActiveLayer);
        })(archItems[ai]);
      }

      for (var li = 0; li < archLayers.length; li++) {
        (function (layer) {
          var index = Number(layer.getAttribute('data-layer'));
          layer.addEventListener('mouseenter', function () { setActiveLayer(index); });
          layer.addEventListener('mouseleave', clearActiveLayer);
          layer.addEventListener('click', function () {
            var target = document.querySelector('[data-layer-link="' + index + '"]');
            if (target && target.href) window.location.href = target.href;
          });
        })(archLayers[li]);
      }

      if (archLayers.length) setActiveLayer(0);

      /* 4. 终端复制。优先 Clipboard API；不可用时退回选中 + execCommand。 */
      var copyButtons = document.querySelectorAll('[data-tm-copy]');
      var legacyCopy = function (text) {
        var area = document.createElement('textarea');
        area.value = text;
        area.setAttribute('readonly', 'readonly');
        area.style.position = 'fixed';
        area.style.left = '-9999px';
        document.body.appendChild(area);
        area.select();
        try {
          document.execCommand('copy');
        } catch (err) {
          /* 忽略：下面统一给"复制失败"的反馈 */
        }
        document.body.removeChild(area);
      };

      for (var c = 0; c < copyButtons.length; c++) {
        (function (button) {
          button.addEventListener('click', function () {
            var source = document.getElementById(button.getAttribute('data-tm-copy'));
            if (!source) return;
            var text = source.innerText || source.textContent || '';
            var done = function () {
              button.textContent = '已复制';
              button.classList.add('is-copied');
              setTimeout(function () {
                button.textContent = '复制';
                button.classList.remove('is-copied');
              }, 1800);
            };
            if (navigator.clipboard && window.isSecureContext) {
              navigator.clipboard.writeText(text).then(done, function () {
                legacyCopy(text);
                done();
              });
            } else {
              legacyCopy(text);
              done();
            }
          });
        })(copyButtons[c]);
      }
    }

    /* 上一页 / 下一页快捷键：主题自带的脚本只认 .navbar 里的 rel=prev/next，
       这里换成页面底部的翻页链接。 */
    var shortcuts = window.shortcuts || null;
    if (shortcuts) {
      document.addEventListener('keydown', function (event) {
        var target = event.target;
        if (target && (target.tagName === 'INPUT' || target.tagName === 'TEXTAREA' || target.isContentEditable)) {
          return;
        }
        var key = event.which || event.keyCode;
        var rel = null;
        if (key === shortcuts.next) {
          rel = 'next';
        } else if (key === shortcuts.previous) {
          rel = 'prev';
        }
        if (!rel) {
          return;
        }
        var link = document.querySelector('.tm-doc-body a[rel="' + rel + '"]');
        if (link && link.href) {
          window.location.href = link.href;
        }
      });
    }
  });
})();
