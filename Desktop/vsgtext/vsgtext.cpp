#include <vsg/all.h>
#include <iostream>

#ifdef USE_VSGXCHANGE
#include <vsgXchange/ReaderWriter_all.h>
#include <vsgXchange/ShaderCompiler.h>
#endif

namespace text
{
    struct GlyphData
    {
        uint16_t character;
        vsg::vec4 uvrect; // min x/y, max x/y
        vsg::vec2 size; // normalised size of the glyph
        vsg::vec2 offset; // normalised offset
        float xadvance; // normalised xadvance
        float lookupOffset; // offset into lookup texture
    };
    using GlyphMap = std::map<uint16_t, GlyphData>;

    bool readUnity3dFontMetaFile(const std::string& filePath, GlyphMap& glyphMap, float& fontPixelHeight, float& normalisedLineHeight)
    {
        std::cout<<"filePath = "<<filePath<<std::endl;

        if (filePath.empty()) return false;

        // read glyph data from txt file
        auto startsWith = [](const std::string& str, const std::string& match)
        {
            return str.compare(0, match.length(), match) == 0;
        };

        auto split = [](const std::string& str, const char& seperator)
        {
            std::vector<std::string> elements;

            std::string::size_type prev_pos = 0, pos = 0;

            while ((pos = str.find(seperator, pos)) != std::string::npos)
            {
                auto substring = str.substr(prev_pos, pos - prev_pos);
                elements.push_back(substring);
                prev_pos = ++pos;
            }

            elements.push_back(str.substr(prev_pos, pos - prev_pos));

            return elements;
        };

        auto uintValueFromPair = [&split](const std::string& str)
        {
            std::vector<std::string> els = split(str, '=');
            return static_cast<uint32_t>(atoi(els[1].c_str()));
        };

        auto floatValueFromPair = [&split](const std::string& str)
        {
            std::vector<std::string> els = split(str, '=');
            return static_cast<float>(atof(els[1].c_str()));
        };

        auto stringValueFromPair = [&split](const std::string& str)
        {
            std::vector<std::string> els = split(str, '=');
            return els[1];
        };


        std::ifstream in(filePath);

        // read header lines
        std::string infoline;
        std::getline(in, infoline);
        std::vector<std::string> infoelements = split(infoline, ' ');
        std::string facename = stringValueFromPair(infoelements[1]);

        fontPixelHeight = floatValueFromPair(infoelements[2]);

        // read common line
        std::string commonline;
        std::getline(in, commonline);
        std::vector<std::string> commonelements = split(commonline, ' ');

        float lineHeight = floatValueFromPair(commonelements[1]);
        normalisedLineHeight = lineHeight / fontPixelHeight;

        float baseLine = floatValueFromPair(commonelements[2]);
        float normalisedBaseLine = baseLine / fontPixelHeight;
        float scaleWidth = floatValueFromPair(commonelements[3]);
        float scaleHeight = floatValueFromPair(commonelements[4]);

        // read page id line
        std::string pageline;
        std::getline(in, pageline);

        // read character count line
        std::string charsline;
        std::getline(in, charsline);
        std::vector<std::string> charselements = split(charsline, ' ');
        uint32_t charscount = uintValueFromPair(charselements[1]);

        // read character data lines
        for (uint32_t i = 0; i < charscount; i++)
        {
            std::string line;
            std::getline(in, line);
            std::vector<std::string> elements = split(line, ' ');

            GlyphData glyph;

            glyph.character = uintValueFromPair(elements[1]);

            // pixel rect of glyph
            float x = floatValueFromPair(elements[2]);
            float y = floatValueFromPair(elements[3]);
            float width = floatValueFromPair(elements[4]);
            float height = floatValueFromPair(elements[5]);

            // adujst y to bottom origin
            y = scaleHeight - (y + height);

            // offset for character glyph in a string
            float xoffset = floatValueFromPair(elements[6]);
            float yoffset = floatValueFromPair(elements[7]);
            float xadvance = floatValueFromPair(elements[8]);

            // calc uv space rect
            vsg::vec2 uvorigin = vsg::vec2(x / scaleWidth, y / scaleHeight);
            vsg::vec2 uvsize = vsg::vec2(width / scaleWidth, height / scaleHeight);
            glyph.uvrect = vsg::vec4(uvorigin.x, uvorigin.y, uvsize.x, uvsize.y);

            // calc normaised size
            glyph.size = vsg::vec2(width / fontPixelHeight, height / fontPixelHeight);

            // calc normalise offsets
            glyph.offset = vsg::vec2(xoffset / fontPixelHeight, normalisedBaseLine - glyph.size.y - (yoffset / fontPixelHeight));
            glyph.xadvance = xadvance / fontPixelHeight;

            // (the font object will calc this)
            glyph.lookupOffset = 0.0f;

            // add glyph to list
            glyphMap[glyph.character] = glyph;
        }
        return true;
    }

    class Font : public vsg::Inherit<vsg::Object, Font>
    {
    public:
        vsg::ref_ptr<vsg::Data> atlas;
        vsg::ref_ptr<vsg::DescriptorImage> texture;
        GlyphMap glyphs;
        float fontHeight;
        float normalisedLineHeight;
        vsg::ref_ptr<vsg::Options> options;

        /// Technique base class provide ability to provide range of different rendering techniques
        class Technique : public vsg::Inherit<vsg::Object, Technique>
        {
        public:
            vsg::ref_ptr<vsg::BindGraphicsPipeline> bindGraphicsPipeline;
            vsg::ref_ptr<vsg::BindDescriptorSet> bindDescriptorSet;
        };

        std::vector<vsg::ref_ptr<Technique>> techniques;

        /// get or create a Technique instance that matches the specified type
        template<class T>
        vsg::ref_ptr<T> getTechnique()
        {
            for(auto& technique : techniques)
            {
                auto tech = technique.cast<T>();
                if (tech) return tech;
            }
            auto tech = T::create(this);
            techniques.emplace_back(tech);
            return tech;
        }
    };

    class StandardText : public vsg::Inherit<Font::Technique, StandardText>
    {
    public:
        StandardText(Font* font)
        {
            auto textureData = font->atlas;

            // load shaders
            auto vertexShader = vsg::read_cast<vsg::ShaderStage>("shaders/text.vert", font->options);
            auto fragmentShader = vsg::read_cast<vsg::ShaderStage>("shaders/text.frag", font->options);

            std::cout<<"vertexShader = "<<vertexShader<<std::endl;
            std::cout<<"fragmentShader = "<<fragmentShader<<std::endl;

            if (!vertexShader || !fragmentShader)
            {
                std::cout<<"Could not create shaders."<<std::endl;
            }

    #ifdef USE_VSGXCHANGE
            // compile section
            vsg::ShaderStages stagesToCompile;
            if (vertexShader && vertexShader->module && vertexShader->module->code.empty()) stagesToCompile.emplace_back(vertexShader);
            if (fragmentShader && fragmentShader->module && fragmentShader->module->code.empty()) stagesToCompile.emplace_back(fragmentShader);

            if (!stagesToCompile.empty())
            {
                auto shaderCompiler = vsgXchange::ShaderCompiler::create();

                std::vector<std::string> defines;
                shaderCompiler->compile(stagesToCompile, defines); // and paths?
            }
            // TODO end of block requiring changes
    #endif

            // set up graphics pipeline
            vsg::DescriptorSetLayoutBindings descriptorBindings
            {
                {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr} // { binding, descriptorTpe, descriptorCount, stageFlags, pImmutableSamplers}
            };

            auto descriptorSetLayout = vsg::DescriptorSetLayout::create(descriptorBindings);

            vsg::PushConstantRanges pushConstantRanges
            {
                {VK_SHADER_STAGE_VERTEX_BIT, 0, 128} // projection view, and model matrices, actual push constant calls autoaatically provided by the VSG's DispatchTraversal
            };

            vsg::VertexInputState::Bindings vertexBindingsDescriptions
            {
                VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}, // vertex data
                VkVertexInputBindingDescription{1, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX}, // colour data
                VkVertexInputBindingDescription{2, sizeof(vsg::vec2), VK_VERTEX_INPUT_RATE_VERTEX}  // tex coord data
            };

            vsg::VertexInputState::Attributes vertexAttributeDescriptions
            {
                VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0}, // vertex data
                VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32_SFLOAT, 0}, // colour data
                VkVertexInputAttributeDescription{2, 2, VK_FORMAT_R32G32_SFLOAT, 0},    // tex coord data
            };

            // alpha blending
            VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT |
                VK_COLOR_COMPONENT_A_BIT;

            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

            auto blending = vsg::ColorBlendState::create(vsg::ColorBlendState::ColorBlendAttachments{colorBlendAttachment});

            // switch off back face culling
            auto rasterization = vsg::RasterizationState::create();
            rasterization->cullMode = VK_CULL_MODE_NONE;

            vsg::GraphicsPipelineStates pipelineStates
            {
                vsg::VertexInputState::create( vertexBindingsDescriptions, vertexAttributeDescriptions ),
                vsg::InputAssemblyState::create(),
                vsg::MultisampleState::create(),
                blending,
                rasterization,
                vsg::DepthStencilState::create()
            };

            auto pipelineLayout = vsg::PipelineLayout::create(vsg::DescriptorSetLayouts{descriptorSetLayout}, pushConstantRanges);
            auto graphicsPipeline = vsg::GraphicsPipeline::create(pipelineLayout, vsg::ShaderStages{vertexShader, fragmentShader}, pipelineStates);
            bindGraphicsPipeline = vsg::BindGraphicsPipeline::create(graphicsPipeline);

            // create texture image and associated DescriptorSets and binding
            auto texture = vsg::DescriptorImage::create(vsg::Sampler::create(), textureData, 0, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
            auto descriptorSet = vsg::DescriptorSet::create(descriptorSetLayout, vsg::Descriptors{texture});

            bindDescriptorSet = vsg::BindDescriptorSet::create(VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, descriptorSet);
        }
    };


    class Text : public vsg::Inherit<vsg::Node, Text>
    {
    public:
        vsg::ref_ptr<Font> font;
        vsg::ref_ptr<Font::Technique> technique;
        vsg::vec3 position;
        std::string text;
    };

    vsg::ref_ptr<Font> readFont(const std::string& fontName, vsg::ref_ptr<vsg::Options> options)
    {

        vsg::Path font_textureFile(vsg::concatPaths("fonts", fontName) + ".vsgb");
        vsg::Path font_metricsFile(vsg::concatPaths("fonts", fontName) + ".txt");

        auto font = Font::create();

        font->options = options;

        font->atlas = vsg::read_cast<vsg::Data>(vsg::findFile(font_textureFile, options->paths));
        if (!font->atlas)
        {
            std::cout<<"Could not read texture file : "<<font_textureFile<<std::endl;
            return {};
        }

        if (!text::readUnity3dFontMetaFile(vsg::findFile(font_metricsFile, options->paths), font->glyphs, font->fontHeight, font->normalisedLineHeight))
        {
            std::cout<<"Could not reading font metrics file"<<std::endl;
            return {};
        }

        // read texture image
        return font;
    }

    vsg::ref_ptr<vsg::Node> createText(const vsg::vec3& position, vsg::ref_ptr<Font> font, const std::string& text)
    {
        return {};
    }

}

int main(int argc, char** argv)
{
    // set up defaults and read command line arguments to override them
    vsg::CommandLine arguments(&argc, argv);

    auto windowTraits = vsg::WindowTraits::create();
    windowTraits->debugLayer = arguments.read({"--debug","-d"});
    windowTraits->apiDumpLayer = arguments.read({"--api","-a"});
    arguments.read({"--window", "-w"}, windowTraits->width, windowTraits->height);

    if (arguments.errors()) return arguments.writeErrorMessages(std::cerr);

    // set up search paths to SPIRV shaders and textures
    vsg::Paths searchPaths = vsg::getEnvPaths("VSG_FILE_PATH");

    auto options = vsg::Options::create();
    options->paths = searchPaths;
    #ifdef USE_VSGXCHANGE
    options->readerWriter = vsgXchange::ReaderWriter_all::create();
    #endif

    auto font = text::readFont("roboto", options);
    std::cout<<"font = "<<font<<std::endl;

    if (!font) return 1;

    auto technique = font->getTechnique<text::StandardText>();
    std::cout<<"technique = "<<technique<<std::endl;
    if (!technique) return 1;


    // create StateGroup as the root of the scene/command graph to hold the GraphicsProgram, and binding of Descriptors to decorate the whole graph
    auto scenegraph = vsg::StateGroup::create();

    scenegraph->add(technique->bindGraphicsPipeline);
    scenegraph->add(technique->bindDescriptorSet);

    // set up model transformation node
    auto transform = vsg::MatrixTransform::create(); // VK_SHADER_STAGE_VERTEX_BIT

    // add transform to root of the scene graph
    scenegraph->addChild(transform);

    // set up vertex and index arrays
    auto vertices = vsg::vec3Array::create(
    {
        {-0.5f, -0.5f, 0.0f},
        {0.5f,  -0.5f, 0.0f},
        {0.5f , 0.5f, 0.0f},
        {-0.5f, 0.5f, 0.0f},
        {-0.5f, -0.5f, -0.5f},
        {0.5f,  -0.5f, -0.5f},
        {0.5f , 0.5f, -0.5f},
        {-0.5f, 0.5f, -0.5f}
    }); // VK_FORMAT_R32G32B32_SFLOAT, VK_VERTEX_INPUT_RATE_INSTANCE, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE

    auto colors = vsg::vec3Array::create(
    {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 1.0f},
    }); // VK_FORMAT_R32G32B32_SFLOAT, VK_VERTEX_INPUT_RATE_VERTEX, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE

    auto texcoords = vsg::vec2Array::create(
    {
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f},
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {1.0f, 1.0f},
        {0.0f, 1.0f}
    }); // VK_FORMAT_R32G32_SFLOAT, VK_VERTEX_INPUT_RATE_VERTEX, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE

    auto indices = vsg::ushortArray::create(
    {
        0, 1, 2,
        2, 3, 0,
        4, 5, 6,
        6, 7, 4
    }); // VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE

    // setup geometry
    auto drawCommands = vsg::Commands::create();
    drawCommands->addChild(vsg::BindVertexBuffers::create(0, vsg::DataList{vertices, colors, texcoords}));
    drawCommands->addChild(vsg::BindIndexBuffer::create(indices));
    drawCommands->addChild(vsg::DrawIndexed::create(12, 1, 0, 0, 0));

    // add drawCommands to transform
    transform->addChild(drawCommands);

    // create the viewer and assign window(s) to it
    auto viewer = vsg::Viewer::create();

    auto window = vsg::Window::create(windowTraits);
    if (!window)
    {
        std::cout<<"Could not create windows."<<std::endl;
        return 1;
    }

    viewer->addWindow(window);

    // camera related details
    auto viewport = vsg::ViewportState::create(window->extent2D());
    auto perspective = vsg::Perspective::create(60.0, static_cast<double>(window->extent2D().width) / static_cast<double>(window->extent2D().height), 0.1, 10.0);
    auto lookAt = vsg::LookAt::create(vsg::dvec3(1.0, 1.0, 1.0), vsg::dvec3(0.0, 0.0, 0.0), vsg::dvec3(0.0, 0.0, 1.0));
    auto camera = vsg::Camera::create(perspective, lookAt, viewport);

    auto commandGraph = vsg::createCommandGraphForView(window, camera, scenegraph);
    viewer->assignRecordAndSubmitTaskAndPresentation({commandGraph});

    // compile the Vulkan objects
    viewer->compile();

    // assign Trackball
    viewer->addEventHandler(vsg::Trackball::create(camera));

    // assign a CloseHandler to the Viewer to respond to pressing Escape or press the window close button
    viewer->addEventHandlers({vsg::CloseHandler::create(viewer)});

    // main frame loop
    while (viewer->advanceToNextFrame())
    {
        // pass any events into EventHandlers assigned to the Viewer
        viewer->handleEvents();

        viewer->update();

        viewer->recordAndSubmit();

        viewer->present();
    }

    // clean up done automatically thanks to ref_ptr<>
    return 0;
}
