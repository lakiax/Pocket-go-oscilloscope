% process_sprites_no_toolbox.m
% 这是一个无需 Image Processing Toolbox 的版本

clear; clc;

% --- 配置参数 ---
input_files = dir('frame_*.png'); % 读取所有 frame_ 开头的 png 文件
target_height = 50;               % 掌机上人物的目标高度 (像素)
transparent_color = [255, 0, 255]; % 洋红色 (R, G, B) 用于透明
white_threshold = 250;            % 背景白色的阈值 (0-255)

% 检查文件是否存在
if isempty(input_files)
    error('未找到 frame_*.png 文件，请检查路径。');
end

% --- 第一步：预读取并计算统一的裁剪框 ---
num_imgs = length(input_files);
% 初始化边界：行(min_r, max_r) 和 列(min_c, max_c)
min_r = inf; max_r = 0;
min_c = inf; max_c = 0;

fprintf('正在分析图片边界 (不使用工具箱)...\n');
for k = 1:num_imgs
    fname = input_files(k).name;
    img = imread(fname); % imread 是基础函数
    
    % 简单的二值化：寻找非白色区域
    % 注意：这里不使用 rgb2gray，直接判断三个通道
    is_foreground = (img(:,:,1) < white_threshold) | ...
                    (img(:,:,2) < white_threshold) | ...
                    (img(:,:,3) < white_threshold);
    
    % 使用 find 获取前景的坐标索引
    [rows, cols] = find(is_foreground);
    
    if ~isempty(rows)
        min_r = min(min_r, min(rows));
        max_r = max(max_r, max(rows));
        min_c = min(min_c, min(cols));
        max_c = max(max_c, max(cols));
    end
end

% 增加安全边距
padding = 5;
[img_h, img_w, ~] = size(img);
min_r = max(1, min_r - padding);
max_r = min(img_h, max_r + padding);
min_c = max(1, min_c - padding);
max_c = min(img_w, max_c + padding);

fprintf('裁剪范围: 行 %d-%d, 列 %d-%d\n', min_r, max_r, min_c, max_c);

% --- 第二步：处理每一帧并拼接 ---
frames = {}; % 用于存储处理后的小图

fprintf('正在处理每一帧...\n');
for k = 1:num_imgs
    fname = input_files(k).name;
    raw_img = imread(fname);
    
    % 1. 裁剪 (替代 imcrop：使用矩阵索引)
    cropped_img = raw_img(min_r:max_r, min_c:max_c, :);
    
    % 2. 缩放 (替代 imresize：使用 interp2 插值)
    [old_h, old_w, ~] = size(cropped_img);
    scale_factor = target_height / old_h;
    new_w = round(old_w * scale_factor);
    new_h = target_height;
    
    % 生成坐标网格
    [X, Y] = meshgrid(1:old_w, 1:old_h);
    [Xq, Yq] = meshgrid(linspace(1, old_w, new_w), linspace(1, old_h, new_h));
    
    % 对 R, G, B 三个通道分别进行双线性插值
    resized_img = zeros(new_h, new_w, 3, 'uint8');
    for c = 1:3
        % interp2 是 MATLAB 核心函数，不需要工具箱
        chan = double(cropped_img(:,:,c));
        resized_chan = interp2(X, Y, chan, Xq, Yq, 'linear'); 
        resized_img(:,:,c) = uint8(resized_chan);
    end
    
    % 3. 去除白底 (替换为洋红色)
    is_bg = (resized_img(:,:,1) > white_threshold) & ...
            (resized_img(:,:,2) > white_threshold) & ...
            (resized_img(:,:,3) > white_threshold);
            
    final_frame = resized_img;
    
    r_ch = final_frame(:,:,1); r_ch(is_bg) = transparent_color(1);
    g_ch = final_frame(:,:,2); g_ch(is_bg) = transparent_color(2);
    b_ch = final_frame(:,:,3); b_ch(is_bg) = transparent_color(3);
    
    final_frame(:,:,1) = r_ch;
    final_frame(:,:,2) = g_ch;
    final_frame(:,:,3) = b_ch;
    
    frames{k} = final_frame;
end

% --- 第三步：横向拼接 ---
% 找出最大宽度以保持对齐
max_w = 0;
for k = 1:num_imgs
    max_w = max(max_w, size(frames{k}, 2));
end
% 宽度偶数化
if mod(max_w, 2) ~= 0
    max_w = max_w + 1;
end

fprintf('统一单帧尺寸: %dx%d\n', max_w, target_height);

sprite_sheet = [];
for k = 1:num_imgs
    curr = frames{k};
    [h, w, ~] = size(curr);
    
    % 创建底板
    canvas = uint8(ones(target_height, max_w, 3));
    canvas(:,:,1) = transparent_color(1);
    canvas(:,:,2) = transparent_color(2);
    canvas(:,:,3) = transparent_color(3);
    
    % 居中放置
    start_x = floor((max_w - w) / 2) + 1;
    
    % 防止舍入误差导致索引越界
    end_x = start_x + w - 1;
    if end_x > max_w
        start_x = start_x - 1;
        end_x = end_x - 1;
    end
    
    canvas(1:h, start_x:end_x, :) = curr;
    
    % 拼接
    sprite_sheet = [sprite_sheet, canvas];
end

% --- 第四步：保存 ---
output_filename = 'walk.bmp';
imwrite(sprite_sheet, output_filename);

fprintf('处理完成！已生成 %s\n', output_filename);
fprintf('Sprite总尺寸: %dx%d, 共 %d 帧\n', size(sprite_sheet, 2), size(sprite_sheet, 1), num_imgs);
fprintf('请记下这个宽度用于 C 代码宏定义: %d\n', max_w);

% 显示结果
imshow(sprite_sheet);
title('生成的 Sprite Sheet');