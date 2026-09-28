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

    /* 首页顶部：暗色 hero 上让顶栏透明，滚下去再变实心 */
    if (header && isHome) {
      var syncHeader = function () {
        header.classList.toggle('is-over', window.scrollY < 40);
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
